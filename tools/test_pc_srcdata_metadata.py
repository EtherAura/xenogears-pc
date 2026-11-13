#!/usr/bin/env python3
"""tools/pc_srcdata.py's plan (--write-plan / --plan): the producer reading the module's file, the
file's schema, both regions, its round trip, its refusals, that both routes link and generate the
same outputs, and that a link given the plan never reaches bin/ or disc/ (a guard that refuses them,
checked first on pc_srcdata's own reader). Invented C, invented images, under TMPDIR: no disc."""
import builtins
import contextlib
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pc_link  # noqa: E402
import pc_srcdata  # noqa: E402

BOOT = {"us": "SYNTH_US.00", "jp": "SYNTH_JP.00"}
AT = {"us": 0x9000, "jp": 0x9100}  # where each region's invented image holds the objects' data
DISC_OWN = 0x1000  # inside main's disc-specific tables (file offsets 0x804..0x8884)
A_C = """
const char synth_text[] = "Invented test words: alpha beta gamma delta";
struct S { int a; const char *p; char t[24]; } synth_rec = { 0x13572468, synth_text, "omega sigma kappa lambda" };
int synth_plain[4] = { 1, 0, 0, 0 };
"""
B_C = 'const char synth_more[] = "Another invented line, nothing retail";\n'
NEW_RUN = 'const char synth_new[] = "A run added after the plan was made";\n'
OWN_C = 'const char synth_own[] = "Only in the disc-specific tables!";\n'


def sections(obj: Path, names) -> bytes:
    out = b""
    for n in names:
        with tempfile.NamedTemporaryFile(dir=obj.parent) as t:
            subprocess.run(["objcopy", "-O", "binary", f"--only-section={n}", str(obj), t.name], check=True)
            out += Path(t.name).read_bytes()
    return out


@contextlib.contextmanager
def disc_closed(root: Path):
    """Every open, stat, listing or command naming root/bin or root/disc fails and is recorded."""
    tried = []
    closed = [os.path.abspath(root / "bin"), os.path.abspath(root / "disc")]

    def check(path):
        if isinstance(path, (str, bytes, os.PathLike)):
            p = os.path.abspath(os.fsdecode(os.fspath(path)))
            if any(p == c or p.startswith(c + os.sep) for c in closed):
                tried.append(p)
                raise PermissionError(f"refused: {p}")

    def guarded(real):
        def f(path=".", *a, **k):
            check(path)
            return real(path, *a, **k)
        return f

    def guarded_cmd(real):
        def f(cmd, *a, **k):
            for c in [cmd] if isinstance(cmd, (str, bytes, os.PathLike)) else cmd:
                check(c)
            return real(cmd, *a, **k)
        return f
    names = ["builtins.open", "io.open", "os.open", "os.stat", "os.lstat", "os.listdir", "os.scandir"]
    reals = [builtins.open, io.open, os.open, os.stat, os.lstat, os.listdir, os.scandir]
    with contextlib.ExitStack() as stack:
        for n, r in zip(names, reals):
            stack.enter_context(mock.patch(n, guarded(r)))
        stack.enter_context(mock.patch("subprocess.run", guarded_cmd(subprocess.run)))
        stack.enter_context(mock.patch("subprocess.Popen", guarded_cmd(subprocess.Popen)))
        yield tried


class Plan(unittest.TestCase):
    ver = "us"

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "src").mkdir()
        # c: data no image holds (a section the plan lists, not stripped).
        self.objs = [self.cc("a", A_C), self.cc("b", B_C), self.cc("c", "int synth_local[4] = {9, 8, 7, 6};\n")]
        self.gen = self.cc("gen", "int synth_gen = 1;\n")
        image = bytearray(0xA000)
        data = b"".join(sections(o, [".data", ".rodata"]) for o in self.objs[:2])
        image[AT[self.ver]:AT[self.ver] + len(data)] = data
        own = OWN_C.split('"')[1].encode()
        image[DISC_OWN:DISC_OWN + len(own)] = own
        new = NEW_RUN.split('"')[1].encode()  # retail too: a run the plan does not know of
        image[AT[self.ver] + 0x800:AT[self.ver] + 0x800 + len(new)] = new
        for ver in ("us", "jp"):
            (self.root / "config" / ver).mkdir(parents=True)
            (self.root / "disc" / ver).mkdir(parents=True)
            img = bytes(image) if ver == self.ver else bytes(image[:-16]) + b"other region img"
            (self.root / "disc" / ver / "main.exe").write_bytes(img)
            (self.root / "config" / ver / "disc.yaml").write_text(
                f"files:\n  - {{name: main, iso: {BOOT[ver]}, vram: 0x80010000, "
                f"sha1: {hashlib.sha1(img).hexdigest()}}}\n")
        (self.root / "bin").mkdir()
        self.patches = [mock.patch.object(pc_link, "ROOT", self.root),
                        mock.patch.dict(os.environ, {k: v for k, v in os.environ.items() if k != "XG_SRCDATA"},
                                        clear=True)]
        for p in self.patches:
            p.start()
        self.plan = self.root / "work" / "plan.json"
        self.plan.parent.mkdir()

    def tearDown(self):
        for p in reversed(self.patches):
            p.stop()
        self.tmp.cleanup()

    def cc(self, name: str, text: str) -> Path:
        c = self.root / "src" / f"{name}.c"
        c.write_text(text)
        o = self.root / "src" / f"{name}.c.o"
        subprocess.run(["gcc", "-c", "-O0", "-fno-PIE", "-fno-data-sections", "-fno-common", "-o", str(o), str(c)],
                       check=True)
        return o

    def run_main(self, route: str, *extra, ver=None, objs=None) -> dict[str, bytes]:
        out = self.root / "out" / route
        out.mkdir(parents=True, exist_ok=True)
        names = ["main.o.partial", "main.src.c", "main.srcsyms"]
        with contextlib.redirect_stdout(io.StringIO()):
            rc = pc_srcdata.main(["--ver", ver or self.ver, *extra, "main", *(str(out / n) for n in names),
                                  str(self.gen), *map(str, objs or self.objs)])
        self.assertEqual(rc, 0)
        return {n: (out / n).read_bytes() for n in names}

    def write_plan(self) -> dict:
        self.run_main("writer", "--write-plan", str(self.plan))
        return json.loads(self.plan.read_text())

    def refuses(self, text: str, *, ver=None, objs=None, want="plan"):
        self.plan.write_text(text)
        with disc_closed(self.root) as tried, self.assertRaises(SystemExit) as e:
            self.run_main("refused", "--plan", str(self.plan), ver=ver, objs=objs)
        self.assertIn(want, str(e.exception))
        self.assertEqual(tried, [])

    def mutated(self, fn) -> None:
        doc = json.loads(self.plan.read_text())
        fn(doc)
        self.refuses(json.dumps(doc))

    # The routes.
    def test_routes_give_the_same_outputs(self):
        default = self.run_main("default")
        doc = self.write_plan()
        first = self.plan.read_bytes()
        self.assertEqual(self.run_main("writer", "--write-plan", str(self.plan)), default)
        self.assertEqual(self.plan.read_bytes(), first)  # deterministic
        self.assertEqual(json.dumps(doc, indent=1, sort_keys=True) + "\n", first.decode())  # round trip
        with disc_closed(self.root) as tried:
            got = self.run_main("metadata", "--plan", str(self.plan))
        self.assertEqual(tried, [])
        self.assertEqual(got, default)
        self.assertIn(b"xgSrcCount_main = ", got["main.src.c"])
        self.assertNotIn(b"xgSrcCount_main = 0;", got["main.src.c"])
        self.assertEqual(doc["region"], self.ver)
        self.assertEqual(doc["boot"], BOOT[self.ver])
        self.assertEqual([o["name"] for o in doc["objects"]], ["src/a.c.o", "src/b.c.o", "src/c.c.o"])
        self.assertEqual(doc["gen_object"], "src/gen.c.o")
        self.assertNotIn(str(self.root), first.decode())

    def test_relocations_are_kept(self):
        self.write_plan()
        with disc_closed(self.root):
            self.run_main("metadata", "--plan", str(self.plan))
        relocs = subprocess.run(["objdump", "-r", "-j", "xgdata_main", str(self.root / "out/metadata/main.o.partial")],
                                capture_output=True, text=True, check=True).stdout
        self.assertIn("synth_text", relocs)

    def test_guard_refuses_the_disc_reader(self):
        with disc_closed(self.root) as tried, self.assertRaises((SystemExit, PermissionError)):
            self.run_main("control")
        self.assertTrue(tried)

    def test_off(self):
        self.write_plan()
        with mock.patch.dict(os.environ, {"XG_SRCDATA": "off"}):
            self.assertIn(b"xgSrcCount_main = 0;", self.run_main("off")["main.src.c"])
            for opt in ("--plan", "--write-plan"):
                with self.assertRaises(SystemExit):
                    self.run_main("off", opt, str(self.plan))

    def test_disc_specific_bytes_are_not_a_source(self):
        objs = [*self.objs, self.cc("own", OWN_C)]
        with self.assertRaises(SystemExit) as e:
            self.run_main("own", objs=objs)
        self.assertIn("does not hold", str(e.exception))

    # The refusals.
    def test_missing_unreadable_and_not_json(self):
        self.refuses("{")
        self.refuses("[]")
        for path in (self.root / "work" / "absent.json", self.root / "work"):
            with disc_closed(self.root) as tried, self.assertRaises(SystemExit) as e:
                self.run_main("refused", "--plan", str(path))
            self.assertIn("plan", str(e.exception))
            self.assertEqual(tried, [])

    def test_header_refusals(self):
        self.write_plan()
        text = self.plan.read_text()
        self.refuses(text.replace('"min": 16', '"min": 16, "min": 16'))
        self.refuses(text, ver="jp" if self.ver == "us" else "us")
        for fn in (lambda d: d.update(format="other"), lambda d: d.update(version="1"),
                   lambda d: d.update(version=True), lambda d: d.update(version=2),
                   lambda d: d.update(region="eu"), lambda d: d.update(boot="OTHER.00"),
                   lambda d: d.update(module="ovl"), lambda d: d["image"].update(sha1="0" * 40),
                   lambda d: d["image"].update(size=True), lambda d: d["image"].update(size=-1),
                   lambda d: d["image"].update(size=2 ** 32), lambda d: d["image"].update(extra=1),
                   lambda d: d.update(min=8), lambda d: d.update(min=True), lambda d: d.update(gen_object="x.o"),
                   lambda d: d.update(hash=d["hash"].upper()), lambda d: d.update(hash=f"{int(d['hash'], 16) ^ 1:016x}"),
                   lambda d: d.update(unknown=1), lambda d: d.pop("hash"), lambda d: d.pop("objects")):
            self.mutated(fn)

    def test_object_and_section_refusals(self):
        doc = self.write_plan()
        secs = doc["objects"][0]["sections"]
        k = next(i for i, s in enumerate(secs) if s["copies"])
        plain = next(i for i, s in enumerate(doc["objects"][2]["sections"]) if s["symbol"] is None)
        for fn in (lambda d: d["objects"][2]["sections"][plain].update(copies=[[0, 0, 1]]),
                   lambda d: d["objects"][2]["sections"][plain].update(symbol=0),lambda d: d["objects"].append(d["objects"][0]), lambda d: d["objects"].pop(),
                   lambda d: d["objects"].reverse(), lambda d: d["objects"][1].update(name=d["objects"][0]["name"]),
                   lambda d: d["objects"][0]["sections"].append(d["objects"][0]["sections"][0]),
                   lambda d: d["objects"][0]["sections"].pop(), lambda d: d["objects"][0].update(extra=1),
                   lambda d: d["objects"][0]["sections"][k].update(size=d["objects"][0]["sections"][k]["size"] + 1),
                   lambda d: d["objects"][0]["sections"][k].update(sha256="0" * 64),
                   lambda d: d["objects"][0]["sections"][k].update(relocs="0" * 64),
                   lambda d: d["objects"][0]["sections"][k].update(symbol=5),
                   lambda d: d["objects"][0]["sections"][k].update(symbol=True),
                   lambda d: d["objects"][0]["sections"][k].pop("symbol"),
                   lambda d: d["objects"][0]["sections"][k].update(copies=[])):
            self.mutated(fn)

    def test_copy_refusals(self):
        doc = self.write_plan()
        secs = doc["objects"][0]["sections"]
        k = next(i for i, s in enumerate(secs) if s["copies"])
        size = secs[k]["size"]

        def set_copy(c):
            return lambda d: d["objects"][0]["sections"][k]["copies"].__setitem__(0, c)
        first = secs[k]["copies"][0]
        for fn in (set_copy([-1, first[1], first[2]]), set_copy([first[0], first[1], 0]),
                   set_copy([first[0], first[1], True]), set_copy(first[:2]), set_copy([size, first[1], 1]),
                   set_copy([first[0], 0xA000, first[2]]), set_copy([first[0], DISC_OWN, first[2]]),
                   set_copy([0, first[1], size]),  # over the section's relocated or zero bytes
                   lambda d: d["objects"][0]["sections"][k]["copies"].append(first)):
            self.mutated(fn)

    def test_stale_plan_with_a_new_run(self):
        self.write_plan()
        (self.root / "src" / "b.c").write_text(B_C + NEW_RUN)
        self.cc("b", B_C + NEW_RUN)
        self.refuses(self.plan.read_text(), want="stale")


class PlanJP(Plan):
    ver = "jp"


if __name__ == "__main__":
    if not shutil.which("gcc") or not shutil.which("objcopy"):
        raise SystemExit("test_pc_srcdata_metadata: needs gcc and binutils")
    unittest.main()
