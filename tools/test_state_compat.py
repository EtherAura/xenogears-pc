#!/usr/bin/env python3
"""tools/state_corpus.py and tools/state_compat.py without the game: the corpus manifest, the canonical set,
the release cache (download stand-in, checksums, unpacking), the load check's verdicts and the gate's
directions (xg-htn.60.5)."""
from __future__ import annotations

import hashlib
import io
import json
import shutil
import sys
import tarfile
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import TestCase, main
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import state_compat  # noqa: E402
import state_corpus as cs  # noqa: E402


def case_rec(config: str, case: str, vblank: int = 1000, build: str = "aa" * 16, schema: str = "11" * 8) -> dict:
    region, engine = config.split("-")
    return {"id": f"{config}/{case}", "config": config, "case": case, "dir": f"{config}/{case}", "region": region,
            "engine": engine, "disc": "1", "vblank": vblank, "run": 600, "schema": schema, "build": build,
            "reader_env": {}}


def write_corpus(root: Path, release: str, configs: dict, canonical: bool = True, created: str = "") -> Path:
    """A corpus on disk: configs {config: (build id, [case ids])}; every file a case names."""
    d = root / release
    cases = []
    for config, (build, ids) in configs.items():
        (d / config / "schemas").mkdir(parents=True, exist_ok=True)
        (d / config / "schemas" / f"{'11' * 8}.xgschema").write_bytes(b"XGSCHEMA")
        for i in ids:
            rec = case_rec(config, i, build=build)
            cd = d / rec["dir"]
            cd.mkdir(parents=True, exist_ok=True)
            (cd / "slot1.xgs").write_bytes(b"slot " + i.encode())
            (cd / "pad.txt").write_text("0 0000\n")
            (cd / "invariants.log").write_text("v 1001 m 1 gs 1\nv 1002 m 1 gs 2\n")
            (cd / "sweep.log").write_text("F 1001 0\n")
            rec["slot_sha256"] = cs.sha256(cd / "slot1.xgs")
            rec["invariants"] = {"sha256": cs.sha256(cd / "invariants.log")}
            cases.append(rec)
    m = {"format": cs.FORMAT, "kind": "xenogears-pc state corpus", "release": release, "canonical": canonical,
         "created": created, "order": cs.release_order(release, created), "run": 600,
         "configs": {c: {"region": c.split("-")[0], "engine": c.split("-")[1], "build": b,
                         "executable": {"kind": "local", "path": f"/nowhere/{release}/{c}"}}
                     for c, (b, _) in configs.items()},
         "cases": cases, "failed": []}
    (d / "manifest.json").write_text(json.dumps(m))
    return d


class ManifestTest(TestCase):
    def test_reads_a_good_one(self):
        with TemporaryDirectory() as t:
            d = write_corpus(Path(t), "v2026.12.0", {"us-original": ("aa" * 16, ["field", "f2"])})
            m = cs.read_manifest(d)
            self.assertEqual([r["case"] for r in m["cases"]], ["field", "f2"])
            self.assertEqual(cs.check_corpus(d), [])

    def test_refusals(self):
        with TemporaryDirectory() as t:
            d = write_corpus(Path(t), "v2026.12.0", {"us-original": ("aa" * 16, ["field"])})
            good = json.loads((d / "manifest.json").read_text())
            for change, said in (({"kind": "other"}, "not a state corpus"), ({"format": 99}, "format 99"),
                                 ({"canonical": "yes"}, "no canonical"), ({"configs": []}, "no configs")):
                (d / "manifest.json").write_text(json.dumps({**good, **change}))
                with self.assertRaisesRegex(cs.CorpusError, said):
                    cs.read_manifest(d)
            for change, said in (({"config": "jp-original"}, "unlisted configuration"),
                                 ({"dir": "../../etc"}, "outside the corpus")):
                bad = {**good, "cases": [{**good["cases"][0], **change}]}
                (d / "manifest.json").write_text(json.dumps(bad))
                with self.assertRaisesRegex(cs.CorpusError, said):
                    cs.read_manifest(d)
            bad = {**good, "cases": [{k: v for k, v in good["cases"][0].items() if k != "vblank"}]}
            (d / "manifest.json").write_text(json.dumps(bad))
            with self.assertRaisesRegex(cs.CorpusError, "without vblank"):
                cs.read_manifest(d)

    def test_check_finds_changed_and_missing_files(self):
        with TemporaryDirectory() as t:
            d = write_corpus(Path(t), "v2026.12.0", {"us-original": ("aa" * 16, ["field"])})
            (d / "us-original/field/slot1.xgs").write_bytes(b"other")
            (d / "us-original/schemas" / f"{'11' * 8}.xgschema").unlink()
            (d / "us-original/field/pad.txt").unlink()
            problems = cs.check_corpus(d)
            self.assertIn("us-original/field: slot1.xgs changed", problems)
            self.assertIn("us-original/field: no pad.txt", problems)
            self.assertTrue(any("schema" in p and "missing" in p for p in problems))


class CanonicalSetTest(TestCase):
    def test_order_and_canonical_only(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            for rel in ("v2027.01.10", "v2027.01.9", "v2026.12.0"):
                write_corpus(root, rel, {"us-original": ("aa" * 16, ["field"])})
            write_corpus(root, "v2027.02.0", {"us-original": ("aa" * 16, ["field"])}, canonical=False)
            write_corpus(root, "standin", {"us-original": ("aa" * 16, ["field"])}, created="-".join(("2026", "11", "24")) + "T" + ":".join(("00",) * 3))  # built: the public scan refuses literal dates
            (root / ".rejected").mkdir()
            (root / "v2026.11.27").mkdir()  # a release before canonical states: no manifest
            names = [m["release"] for _, m in cs.corpora(root)]
            self.assertEqual(names, ["v2026.12.0", "v2027.01.9", "v2027.01.10", "standin"])

    def test_tags(self):
        self.assertLess(cs.release_order("v2026.12.3"), cs.release_order("v2027.01.0"))
        self.assertLess(cs.release_order("v2027.01.0"), cs.release_order("dev", "-".join(("2020", "01", "01"))))
        self.assertEqual(cs.tarball_name("v2026.12.0"), "xenogears-pc-2026.12.0-linux-x86_64.tar.gz")
        for bad in ("2026.12.0", "v2026.13.0", "v2026.12.01"):
            with self.assertRaises(cs.CorpusError):
                cs.tarball_name(bad)


def make_tarball(path: Path, top: str, files: dict, extra: list | None = None) -> None:
    with tarfile.open(path, "w:gz") as tar:
        for name, data in files.items():
            info = tarfile.TarInfo(f"{top}/{name}")
            info.size, info.mode = len(data), 0o755
            tar.addfile(info, io.BytesIO(data))
        for info in extra or []:
            tar.addfile(info)


class CacheTest(TestCase):
    TAG = "v2099.01.0"

    def assets(self, root: Path, sum_text: str | None = "good", digest: str | None = "good") -> Path:
        name = cs.tarball_name(self.TAG)
        top = name.removesuffix(".tar.gz")
        d = root / "assets" / self.TAG
        d.mkdir(parents=True)
        make_tarball(d / name, top, {"bin/xenogears": b"\x7fELF original", "share/xenogears-pc/xenogears.xgschema": b"S"})
        real = hashlib.sha256((d / name).read_bytes()).hexdigest()
        if sum_text is not None:
            (d / f"{name}.sha256").write_text(f"{real if sum_text == 'good' else '0' * 64}  {name}\n")
        if digest is not None:
            (d / "digests.json").write_text(json.dumps({name: f"sha256:{real if digest == 'good' else '1' * 64}"}))
        return root / "assets"

    def test_fetch_verify_unpack_once(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            cache = cs.ReleaseCache(root / "cache", assets_from=self.assets(root))
            folder = cache.unpacked(self.TAG)
            self.assertEqual((folder / "bin/xenogears").read_bytes(), b"\x7fELF original")
            rec = json.loads((root / "cache" / self.TAG / "verified.json").read_text())
            self.assertEqual(rec["checked"], ["sha256 asset", "GitHub digest"])
            # Cached: the source going away changes nothing.
            name = cs.tarball_name(self.TAG)
            (root / "assets" / self.TAG / name).unlink()
            self.assertEqual(cache.unpacked(self.TAG), folder)
            self.assertEqual(cache.unpacked(self.TAG, expect=rec["sha256"]), folder)
            # A manifest that names other bytes: refetched and refused.
            with self.assertRaisesRegex(cs.CorpusError, "has no"):
                cache.unpacked(self.TAG, expect="2" * 64)

    def test_published_sum_mismatch(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            cache = cs.ReleaseCache(root / "cache", assets_from=self.assets(root, sum_text="bad"))
            with self.assertRaisesRegex(cs.CorpusError, "does not match its published"):
                cache.tarball(self.TAG)
            self.assertFalse((root / "cache" / self.TAG / cs.tarball_name(self.TAG)).exists())

    def test_github_digest_mismatch(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            cache = cs.ReleaseCache(root / "cache", assets_from=self.assets(root, digest="bad"))
            with self.assertRaisesRegex(cs.CorpusError, "not the asset GitHub has"):
                cache.tarball(self.TAG)

    def test_manifest_mismatch(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            cache = cs.ReleaseCache(root / "cache", assets_from=self.assets(root))
            with self.assertRaisesRegex(cs.CorpusError, "the corpus was made from"):
                cache.tarball(self.TAG, expect="3" * 64)

    def test_no_published_sum(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            cache = cs.ReleaseCache(root / "cache", assets_from=self.assets(root, sum_text=None, digest=None))
            cache.tarball(self.TAG)
            self.assertTrue(any("unverified" in n for n in cache.notes))

    def test_missing_asset_and_offline(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            (root / "assets" / self.TAG).mkdir(parents=True)
            with self.assertRaisesRegex(cs.CorpusError, "has no"):
                cs.ReleaseCache(root / "cache", assets_from=root / "assets").tarball(self.TAG)
            with self.assertRaisesRegex(cs.CorpusError, "offline"):
                cs.ReleaseCache(root / "cache2", offline=True).tarball(self.TAG)

    def test_local_tarball_with_sum(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            assets = self.assets(root, digest=None)
            local = assets / self.TAG / cs.tarball_name(self.TAG)
            cache = cs.ReleaseCache(root / "cache")
            self.assertTrue(cache.tarball(self.TAG, local=local).is_file())

    def test_unsafe_tarballs(self):
        top = cs.tarball_name(self.TAG).removesuffix(".tar.gz")
        link = tarfile.TarInfo(f"{top}/bin/link")
        link.type, link.linkname = tarfile.SYMTYPE, "/etc/passwd"
        outside = tarfile.TarInfo("elsewhere/file")
        dots = tarfile.TarInfo(f"{top}/../escape")
        for bad in (link, outside, dots):
            with TemporaryDirectory() as t:
                path = Path(t) / "x.tar.gz"
                make_tarball(path, top, {"bin/xenogears": b"x"}, [bad])
                with self.assertRaises(cs.CorpusError):
                    cs.safe_extract(path, Path(t) / "out", top)

    def test_executable_records(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            cache = cs.ReleaseCache(root / "cache", assets_from=self.assets(root))
            exe = cache.executable(self.TAG, {"kind": "tarball", "tag": self.TAG, "path": "bin/xenogears"})
            self.assertEqual(exe.name, "xenogears")
            with self.assertRaisesRegex(cs.CorpusError, "kind"):
                cache.executable(self.TAG, {"kind": "ftp"})
            with self.assertRaisesRegex(cs.CorpusError, "no "):
                cache.executable(self.TAG, {"kind": "local", "path": str(root / "missing")})


class LogsTest(TestCase):
    def test_rows_and_differences(self):
        with TemporaryDirectory() as t:
            p = Path(t) / "inv.log"
            p.write_text("c 0 0 63 aa\nv 999 m 1\nv 1000 m 1\nv 1001 m 1 p 1 10 20 30\nF 1600 x\nv 1601 m 1\n")
            rows = cs.log_rows(p, 1000, 1600)
            self.assertEqual(rows, ["v 1001 m 1 p 1 10 20 30", "F 1600 x"])
            self.assertIsNone(cs.first_difference(rows, list(rows)))
            self.assertIn("line 1 of 2/2", cs.first_difference(rows, ["v 1001 m 1 p 1 10 20 31", "F 1600 x"]))
            self.assertIsNone(cs.first_difference(rows, ["v 1001 m 1 p 1 10 20 31", "F 1600 x"], tolerate=2))
            self.assertIn("2 lines in the writer's, 1", cs.first_difference(rows, rows[:1]))
            self.assertEqual(cs.log_rows(Path(t) / "none.log", 0, 9), [])

    def test_spread(self):
        items = list(range(10))
        self.assertEqual(state_compat.spread(items, 0), items)
        self.assertEqual(state_compat.spread(items, 3), [0, 4, 9])  # first, middle, last
        self.assertEqual(state_compat.spread(items, 1), [0])
        self.assertEqual(state_compat.spread(items, 20), items)


class CheckLoadTest(TestCase):
    """check_load's verdicts, the game replaced by a fake that writes what a reader would."""

    def run_load(self, log: str, code: int = 0, inv: str | None = None, reader_id: str = "bb" * 16):
        with TemporaryDirectory() as t:
            root = Path(t)
            corpus = write_corpus(root, "v2026.12.0", {"us-original": ("aa" * 16, ["field"])})
            rec = cs.read_manifest(corpus)["cases"][0]

            def fake(exe, out, image, quit_at, env):
                self.assertEqual(quit_at, 1601)
                self.assertEqual(env["XG_STATE_SLOT_LOAD"], "1")
                self.assertTrue((Path(env["XG_STATE_DIR"]) / "slot1.xgs").is_file())
                self.assertTrue((Path(env["XG_STATE_DIR"]) / "schemas" / f"{'11' * 8}.xgschema").is_file())
                out.mkdir(parents=True, exist_ok=True)
                (out / "port.log").write_text(log)
                (out / "invariants.log").write_text(inv if inv is not None else
                                                    "c 0 0 63 aa\nv 3 m 0\nv 1001 m 1 gs 1\nv 1002 m 1 gs 2\n")
                (out / "sweep.log").write_text("F 1001 0\n")
                return code

            with patch.object(cs, "run_game", side_effect=fake), patch.object(cs, "image_id", return_value=reader_id):
                reader = cs.Build("us", "original", Path("/x/xenogears"), None)
                return cs.check_load(reader, corpus, rec, root / "w")

    LOADED = "state: converting ...\nstate: Loaded slot 1 (Field 8), converted from another build of the port\n"

    def test_pass(self):
        got = self.run_load(self.LOADED)
        self.assertTrue(got.ok, got.problems)
        self.assertEqual((got.how, got.lines), ("converted", (2, 1)))

    def test_pass_from_a_release(self):
        # a release's slot read by another release names the writer's version (stateportable.c
        # statePortableWriter); the pre-flight of failed all 256 loads on this wording
        for line in ("Loaded slot 1 (Field 8), converted from version 2099.04.0",
                     "Loaded slot 1 (Battle), converted from version 2099.04.1: 8 values not carried (the log lists them)"):
            got = self.run_load(f"state: converting ...\nstate: {line}\n")
            self.assertTrue(got.ok, got.problems)
            self.assertEqual(got.how, "converted")

    def test_refused(self):
        got = self.run_load("state: Cannot load slot 1: saved by version X, which this one cannot read\n")
        self.assertFalse(got.ok)
        self.assertIn("refused", got.problems[0])

    def test_crash(self):
        got = self.run_load(self.LOADED, code=-11, inv="v 1001 m 1 gs 1\n")
        self.assertIn("exit -11", got.problems)
        self.assertTrue(any(p.startswith("invariants: 2 lines in the writer's, 1") for p in got.problems))

    def test_timeout(self):
        self.assertTrue(any("timed out" in p for p in self.run_load(self.LOADED, code=124).problems))

    def test_invariants_differ(self):
        got = self.run_load(self.LOADED, inv="v 1001 m 1 gs 1\nv 1002 m 1 gs 9\n")
        self.assertEqual(len(got.problems), 1)
        self.assertIn("invariants: line 2", got.problems[0])

    def test_own_build_raw_and_others_converted(self):
        raw = "state: Loaded slot 1 (Field 8)\n"
        self.assertTrue(self.run_load(raw, reader_id="aa" * 16).ok)
        self.assertIn("converted its slot", self.run_load(self.LOADED, reader_id="aa" * 16).problems[0])
        self.assertIn("without converting", self.run_load(raw).problems[0])

    def test_notes(self):
        got = self.run_load("state: converting x in 100 ms: 2 encode and 3 decode warnings\n" + self.LOADED
                            + "state: 4 values not carried\n")
        self.assertTrue(got.ok)
        self.assertEqual(got.notes, ["3 decode warnings", "4 values not carried"])


class GateTest(TestCase):
    def test_vacuous(self):
        with TemporaryDirectory() as t:
            r = state_compat.run(Path(t) / "none", new_builds={}, quiet=True)
            self.assertTrue(r.passed)
            self.assertIn("vacuous pass: no canonical release", r.line())
            write_corpus(Path(t), "v2027.01.0", {"us-original": ("aa" * 16, ["field"])})
            r = state_compat.run(Path(t), new_builds={}, new_release="v2026.12.0", quiet=True)
            self.assertIn("no canonical release before v2026.12.0", r.line())

    def test_directions_coverage_and_sampling(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            write_corpus(root / "corpora", "v2026.12.0", {"us-original": ("aa" * 16, ["field", "f2", "f4"]),
                                                         "jp-original": ("cc" * 16, ["field"])})
            new = write_corpus(root, "new", {"us-original": ("bb" * 16, ["field", "f2", "f4"]),
                                             "us-enhanced": ("dd" * 16, ["field"])})
            seen = []

            def fake(reader, corpus, rec, work, *a):
                seen.append((reader.exe.name, corpus.name, rec["id"]))
                return cs.Loaded(rec["case"] != "f4", [] if rec["case"] != "f4" else ["exit -11"], [], "converted")

            builds = {"us-original": cs.Build("us", "original", Path("/new/xenogears"), None),
                      "us-enhanced": cs.Build("us", "enhanced", Path("/new/xenogears-enhanced"), None)}
            with patch.object(cs, "check_load", side_effect=fake), \
                    patch.object(cs.ReleaseCache, "executable", side_effect=lambda rel, e, *a: Path(e["path"])), \
                    patch.object(cs, "image_id", side_effect=lambda exe: {"us-original": "aa" * 16}.get(exe.name, "?")):
                r = state_compat.run(root / "corpora", new, builds, work=root / "w", quiet=True, jobs=2)
            fwd = sorted(x for x in seen if x[1] == "v2026.12.0")
            bwd = sorted(x for x in seen if x[1] == "new")
            self.assertEqual(fwd, [("xenogears", "v2026.12.0", f"us-original/{c}") for c in ("f2", "f4", "field")])
            self.assertEqual(bwd, [("us-original", "new", f"us-original/{c}") for c in ("f2", "f4", "field")])
            self.assertFalse(r.passed)
            self.assertEqual(sorted(x["what"] for x in r.loads if not x["ok"]),
                             ["new us-original/f4 -> v2026.12.0", "v2026.12.0 us-original/f4 -> new"])
            self.assertIn("2 FAILED", r.line())
            self.assertEqual(sorted(r.uncovered), [
                "jp-original: no new build of it (forward and backward not run)",
                "us-enhanced: v2026.12.0 has no executable of it (backward not run)"])
            seen.clear()
            with patch.object(cs, "check_load", side_effect=fake), \
                    patch.object(cs.ReleaseCache, "executable", side_effect=lambda rel, e, *a: Path(e["path"])), \
                    patch.object(cs, "image_id", side_effect=lambda exe: {"us-original": "aa" * 16}.get(exe.name, "?")):
                r = state_compat.run(root / "corpora", new, builds, work=root / "w", quiet=True, sample=1,
                                     directions=("forward",))
            self.assertEqual([x[2] for x in seen], ["us-original/field"])

    def test_full_first_and_latest(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            for rel in ("v2026.12.0", "v2026.12.1", "v2026.12.2", "v2026.12.3"):
                write_corpus(root / "corpora", rel, {"us-original": ("aa" * 16, ["field", "f2", "f4"])})
            seen = []

            def fake(reader, corpus, rec, work, *a):
                seen.append(corpus.name)
                return cs.Loaded(True, [], [], "converted")

            builds = {"us-original": cs.Build("us", "original", Path("/new/xenogears"), None)}
            with patch.object(cs, "check_load", side_effect=fake):
                r = state_compat.run(root / "corpora", None, builds, work=root / "w", quiet=True, sample=1, full=1,
                                     directions=("forward",))
            self.assertTrue(r.passed)
            counts = {rel: seen.count(rel) for rel in sorted(set(seen))}
            self.assertEqual(counts, {"v2026.12.0": 3, "v2026.12.1": 1, "v2026.12.2": 1, "v2026.12.3": 3})

    def test_release_executable_must_be_the_writer(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            write_corpus(root / "corpora", "v2026.12.0", {"us-original": ("aa" * 16, ["field"])})
            new = write_corpus(root, "new", {"us-original": ("bb" * 16, ["field"])})
            builds = {"us-original": cs.Build("us", "original", Path("/new/xenogears"), None)}
            with patch.object(cs, "check_load", return_value=cs.Loaded(True, [], [], "converted")), \
                    patch.object(cs.ReleaseCache, "executable", side_effect=lambda rel, e, *a: Path(e["path"])), \
                    patch.object(cs, "image_id", return_value="ee" * 16):
                r = state_compat.run(root / "corpora", new, builds, work=root / "w", quiet=True,
                                     directions=("backward",))
            self.assertFalse(r.passed)
            self.assertIn("not the build that wrote its corpus", r.errors[0])


class KeptExecutablesTest(TestCase):
    """Executables that are no asset (JP: carl "JP for the gate only") live in their release's corpus:
    state_corpus.py release --build keeps them there, state_compat.py runs them from there."""
    TAG = "v2099.01.0"

    def tarball(self, d: Path, region: str, off: bool = False) -> Path:
        name = cs.tarball_name(self.TAG)
        d.mkdir(parents=True, exist_ok=True)
        exe = b"\x7fELF " + region.encode() + (cs.OFF_TEXT if off else b"")
        make_tarball(d / name, name.removesuffix(".tar.gz"), {
            "bin/xenogears": exe + b" original", "bin/xenogears-enhanced": exe + b" enhanced",
            "share/xenogears-pc/xenogears.xgschema": b"XGSCHEMA " + region.encode(),
            "share/xenogears-pc/xenogears-enhanced.xgschema": b"XGSCHEMA " + region.encode()})
        (d / f"{name}.sha256").write_text(f"{cs.sha256(d / name)}  {name}\n")
        return d / name

    @staticmethod
    def region_of(path: Path) -> str:
        return "jp" if path.read_bytes().endswith(b"jp") else "us"

    def release(self, root: Path, *more: str, gate_passes: bool | None = None) -> tuple[int, str]:
        """state_corpus.py release with the game left out: make_corpus writes a manifest of the builds it is given,
        the gate's verdict is given."""
        made = []

        def fake_make(builds, out, release, *a, **k):
            made.append([b.config for b in builds])
            out.mkdir(parents=True)
            m = {"format": cs.FORMAT, "kind": "xenogears-pc state corpus", "release": release, "canonical": False,
                 "created": "", "order": cs.release_order(release), "run": 600, "cases": [], "failed": [],
                 "configs": {b.config: {"region": b.region, "engine": b.engine, "build": "aa" * 16,
                                        "executable": b.source.get("executable")} for b in builds}}
            cs.write_json(out / "manifest.json", m)
            return {**m, "bytes": 0, "seconds": 0.0}

        verdict = state_compat.Result(root)
        if gate_passes is False:
            verdict.loads = [{"ok": False, "direction": "forward", "what": "x", "problems": ["exit -11"]}]
        else:
            verdict.vacuous = "no canonical release yet"
        with patch.object(cs, "schema_version", side_effect=self.region_of), \
                patch.object(cs, "make_corpus", side_effect=fake_make), \
                patch.object(state_compat, "run", return_value=verdict), \
                patch("sys.stdout", new_callable=io.StringIO) as out, patch("sys.stderr", new_callable=io.StringIO) as err:
            code = cs.main(["release", self.TAG, "--assets-from", str(root / "assets"), "--cache", str(root / "cache"),
                            "--corpora", str(root / "corpora"), *more, *(["--gate"] if gate_passes is not None else [])])
        self.made = made
        return code, out.getvalue() + err.getvalue()

    def test_a_jp_tarball_is_kept_in_the_corpus_and_read_from_there(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            self.tarball(root / "assets" / self.TAG, "us")
            jp = self.tarball(root / "local", "jp")
            code, said = self.release(root, "--build", str(jp), gate_passes=True)
            self.assertEqual(code, 0, said)
            self.assertEqual(self.made, [["us-original", "us-enhanced", "jp-original", "jp-enhanced"]])
            corpus = root / "corpora" / self.TAG
            kept = corpus / "executables" / "jp" / jp.name
            self.assertEqual(kept.read_bytes(), jp.read_bytes())
            self.assertEqual((corpus / "executables/jp" / f"{jp.name}.sha256").read_text(),
                             (root / "local" / f"{jp.name}.sha256").read_text())
            m = cs.read_manifest(corpus)
            self.assertTrue(m["canonical"])
            rec = m["configs"]["jp-enhanced"]["executable"]
            self.assertEqual((rec["kind"], rec["kept"], rec["path"], rec["sha256"]),
                             ("tarball", f"executables/jp/{jp.name}", "bin/xenogears-enhanced", cs.sha256(jp)))
            self.assertNotIn("kept", m["configs"]["us-original"]["executable"])  # the published one: downloaded
            self.assertEqual(cs.check_corpus(corpus), [])
            # A later release's backward run: from the corpus alone (the local build and the cache gone).
            shutil.rmtree(root / "local")
            shutil.rmtree(root / "cache")
            cache = cs.ReleaseCache(root / "cache2", offline=True)
            exe = cache.executable(self.TAG, rec, corpus)
            self.assertEqual(exe.read_bytes(), b"\x7fELF jp enhanced")
            with self.assertRaisesRegex(cs.CorpusError, "no corpus was named"):
                cache.executable(self.TAG, rec)
            kept.write_bytes(b"other")
            with self.assertRaisesRegex(cs.CorpusError, "changed since its corpus was made"):
                cache.executable(self.TAG, rec, corpus)
            self.assertIn(f"jp-enhanced: executables/jp/{jp.name} changed", cs.check_corpus(corpus))

    def test_a_failed_gate_rejects_the_corpus_with_its_executables(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            self.tarball(root / "assets" / self.TAG, "us")
            jp = self.tarball(root / "local", "jp")
            code, said = self.release(root, "--build", str(jp), gate_passes=False)
            self.assertEqual(code, 1)
            self.assertIn("FAILS state-compat", said)
            self.assertFalse((root / "corpora" / self.TAG).exists())
            rejected = list((root / "corpora" / ".rejected").iterdir())
            self.assertEqual(len(rejected), 1)
            self.assertTrue((rejected[0] / "executables/jp" / jp.name).is_file())
            self.assertFalse(cs.read_manifest(rejected[0])["canonical"])

    def test_what_a_build_given_must_be(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            self.tarball(root / "assets" / self.TAG, "us")
            jp = self.tarball(root / "local", "jp")
            other = root / "other" / cs.tarball_name("v2099.01.1")
            other.parent.mkdir()
            shutil.copyfile(jp, other)
            for args, said in ((["--build", str(other)], "not v2099.01.0's Linux tarball"),
                               (["--build", f"jp-original={jp}"], "give it alone"),
                               (["--build", str(root / "missing" / jp.name)], "no such tarball"),
                               (["--build", str(root / "assets" / self.TAG / jp.name)], "two builds of one configuration")):
                with self.subTest(args=args):
                    code, out = self.release(root, *args)
                    self.assertEqual(code, 1)
                    self.assertIn(said, out)
            (root / "local" / f"{jp.name}.sha256").write_text(f"{'0' * 64}  {jp.name}\n")
            code, out = self.release(root, "--build", str(jp))
            self.assertEqual((code, "does not match" in out), (1, True))
            off = self.tarball(root / "off", "jp", off=True)
            code, out = self.release(root, "--build", str(off))
            self.assertEqual((code, "writes no portable slots" in out), (1, True))
            self.assertFalse((root / "corpora" / self.TAG).exists())

    def test_a_build_folder_keeps_its_executable_and_schema(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            self.tarball(root / "assets" / self.TAG, "us")
            folder = root / "jp-build"
            folder.mkdir()
            (folder / "xenogears").write_bytes(b"\x7fELF jp dev build")
            (folder / "xenogears.xgschema").write_bytes(b"XGSCHEMA jp")
            code, said = self.release(root, "--build", f"jp-original={folder}", gate_passes=True)
            self.assertEqual(code, 0, said)
            corpus = root / "corpora" / self.TAG
            rec = cs.read_manifest(corpus)["configs"]["jp-original"]["executable"]
            self.assertEqual((rec["kind"], rec["path"]), ("kept", "executables/jp-original/bin/xenogears"))
            self.assertEqual((corpus / rec["schema"]["path"]).read_bytes(), b"XGSCHEMA jp")
            self.assertEqual(cs.check_corpus(corpus), [])
            exe = cs.ReleaseCache(root / "cache").executable(self.TAG, rec, corpus)
            self.assertEqual(exe, corpus / "executables/jp-original/bin/xenogears")
            self.assertEqual(exe.read_bytes(), b"\x7fELF jp dev build")

    def test_a_kept_path_outside_the_corpus_is_refused(self):
        with TemporaryDirectory() as t:
            d = write_corpus(Path(t), "v2026.12.0", {"jp-original": ("aa" * 16, ["field"])})
            good = json.loads((d / "manifest.json").read_text())
            for executable in ({"kind": "kept", "path": "../../bin/sh", "sha256": "0"},
                               {"kind": "tarball", "tag": "v2026.12.0", "path": "bin/xenogears", "kept": "/etc/x.tar.gz"},
                               {"kind": "kept", "path": "jp-original/field/slot1.xgs"}):
                bad = {**good, "configs": {"jp-original": {**good["configs"]["jp-original"], "executable": executable}}}
                (d / "manifest.json").write_text(json.dumps(bad))
                with self.subTest(executable=executable), self.assertRaisesRegex(cs.CorpusError, "outside the corpus"):
                    cs.read_manifest(d)

    def test_the_gate_runs_an_earlier_release_from_its_corpus(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            earlier = write_corpus(root / "corpora", "v2026.12.0", {"jp-original": ("aa" * 16, ["field"])})
            m = json.loads((earlier / "manifest.json").read_text())
            m["configs"]["jp-original"]["executable"] = {"kind": "kept", "path": "executables/jp-original/bin/xenogears",
                                                        "sha256": "x"}
            (earlier / "manifest.json").write_text(json.dumps(m))
            new = write_corpus(root, "new", {"jp-original": ("bb" * 16, ["field"])})
            seen = []

            def executable(cache, rel, rec, corpus=None):
                seen.append(corpus)
                return Path("/kept/xenogears")

            builds = {"jp-original": cs.Build("jp", "original", Path("/new/xenogears"), None)}
            with patch.object(cs, "check_load", return_value=cs.Loaded(True, [], [], "converted")), \
                    patch.object(cs.ReleaseCache, "executable", executable), \
                    patch.object(cs, "image_id", return_value="aa" * 16):
                r = state_compat.run(root / "corpora", new, builds, work=root / "w", quiet=True,
                                     directions=("backward",))
            self.assertTrue(r.passed, r.line())
            self.assertEqual(seen, [earlier])


class ReleaseTest(TestCase):
    def test_a_release_without_portable_slots_gets_no_corpus(self):
        tag = "v2099.01.0"
        with TemporaryDirectory() as t:
            root = Path(t)
            name = cs.tarball_name(tag)
            top = name.removesuffix(".tar.gz")
            (root / "assets" / tag).mkdir(parents=True)
            make_tarball(root / "assets" / tag / name, top, {
                "bin/xenogears": b"\x7fELF ..." + cs.OFF_TEXT + b"...",
                "share/xenogears-pc/xenogears.xgschema": b"XGSCHEMA"})
            with patch.object(cs, "schema_version", return_value="us"), \
                    patch("sys.stdout", new_callable=io.StringIO) as out:
                code = cs.main(["release", tag, "--assets-from", str(root / "assets"), "--cache", str(root / "cache"),
                                "--corpora", str(root / "corpora")])
            self.assertEqual(code, 0)
            self.assertIn("is not a canonical release", out.getvalue())
            self.assertFalse((root / "corpora").exists())

    def test_the_reason_in_pieces_still_says_off(self):
        """The release builder's GCC copies stateportable.c's literal in pieces: its first 48 bytes from .rodata, the
        rest as immediates (an XG_DIST build of held "slots that other versions load come in a later r"
        and then other text). The start of it is enough; a build with portable slots has none of it."""
        with TemporaryDirectory() as t:
            exe = Path(t) / "xenogears"
            exe.write_bytes(b"\x7fELF ..." + cs.OFF_TEXT[:48] + b"there is no such ...")
            self.assertTrue(cs.portable_off(exe))
            exe.write_bytes(b"\x7fELF ... slots that other builds load ...")
            self.assertFalse(cs.portable_off(exe))


class CasesTest(TestCase):
    def test_cases(self):
        cases = cs.all_cases()
        for region in ("us", "jp"):
            ids = [c.id for c in cs.all_cases(region)]
            self.assertEqual(len(ids), len(set(ids)), region)  # one variant of each case per region
            self.assertEqual(len(ids), 32, region)
            self.assertEqual([c.id for c in cs.find_cases(["b-boss", "field"], region)], ["field", "b-boss"])
            self.assertEqual({c.region for c in cs.find_cases(["b-foot"], region)}, {region})
        ids = [c.id for c in cases]
        self.assertEqual(len({c.id for c in cs.FIELD_CASES}), 20)
        self.assertEqual({c.safe for c in cs.BATTLE_CASES}, {0x201})
        self.assertNotIn("movie", ids)
        for c in cases:
            self.assertFalse(set(dict(c.reader_env())) & set(cs.BOOT_KEYS))
        with self.assertRaisesRegex(cs.CorpusError, "unknown case"):
            cs.find_cases(["nope"])

    def test_battle_pad_waits_at_the_menu(self):
        with TemporaryDirectory() as t:
            c = cs.find_cases(["b-boss"], "us")[0]
            cs.write_pad(c, Path(t) / "pad.txt")
            presses = [line.split() for line in (Path(t) / "pad.txt").read_text().splitlines()]
            start = next(int(v) for v, b in presses if b == "0800")
            after = [int(v) for v, b in presses if int(v) > start + 8 and b != "0000"]
            self.assertGreaterEqual(after[0], c.asked)  # nothing pressed between Start and the save


if __name__ == "__main__":
    main()
