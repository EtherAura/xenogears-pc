#!/usr/bin/env python3
"""The state-compat gate (xg-htn.60.5; docs/CANONICAL_STATES.md 9.3, 17): save states load both ways between
releases. For the new build (a development build, or a release about to be published) and every earlier
canonical release E:

  forward   each state of E's corpus loads in the new build;
  backward  each state of the new build's corpus loads in E's executable (E's published Linux tarball,
            downloaded once into the cache and checked against its published SHA-256; executables that are
            no asset, JP's, were kept in the cache when E's corpus was made);

and in both, a load is held to tools/state_corpus.py check_load: no refusal, no crash, and the invariant and
sweep logs of the 600 vblanks after the load equal the writer's own continuation, line for line.

    tools/state_compat.py --new BUILD [--new BUILD ...] [--corpora DIR]     a development build (the gate)
    tools/state_compat.py --release TAG [--corpora DIR]                     a release whose corpus is filed
    tools/state_compat.py ... [--new-corpus DIR] [--sample N] [--forward-only | --backward-only] [--jobs N]

BUILD as state_corpus.py takes it (a build folder, an install, an unpacked release); one configuration
(region and engine) each. The new build's corpus is made in --work unless --new-corpus names one. The
canonical releases: every corpus under --corpora (work/state-corpus) whose manifest says "canonical": true
(state_corpus.py release writes it), earlier than the new one (a development build comes after them all).
With none the gate passes, saying so: the releases before the first canonical one wrote no portable slots
and are never in the set.

A configuration only one side has (the new build is US only, or a release's JP executables were not kept)
is listed as not covered. Prints one line per load and one summary line starting "state-compat:"; exit 0
when every load passed (or there was nothing to load), 1 otherwise.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import shutil
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import state_corpus as cs  # noqa: E402


@dataclass
class Result:
    root: Path
    earlier: list[str] = field(default_factory=list)
    loads: list[dict] = field(default_factory=list)
    uncovered: list[str] = field(default_factory=list)
    left_out: list[str] = field(default_factory=list)  # cases their own build does not repeat
    errors: list[str] = field(default_factory=list)
    vacuous: str = ""
    seconds: float = 0.0

    @property
    def passed(self) -> bool:
        return not self.errors and all(x["ok"] for x in self.loads)

    def line(self) -> str:
        if self.vacuous:
            return f"state-compat: vacuous pass: {self.vacuous}"
        n = {d: [x for x in self.loads if x["direction"] == d] for d in ("forward", "backward")}
        bad = [x for x in self.loads if not x["ok"]]
        head = (f"state-compat: {len(self.earlier)} earlier canonical release{'s' if len(self.earlier) != 1 else ''} "
                f"({', '.join(self.earlier)}): {len(n['forward'])} forward and {len(n['backward'])} backward loads")
        if self.errors:
            return f"{head}; FAILED: {'; '.join(self.errors)[:300]}"
        n = len(self.left_out)
        left = (f"; {n} case{'s' if n != 1 else ''} left out (not repeated by {'their' if n != 1 else 'its'} own "
                "build)" if n else "")
        if bad:
            return f"{head}; {len(bad)} FAILED (first: {bad[0]['what']}: {bad[0]['problems'][0][:160]}){left}"
        return f"{head}, all pass ({self.seconds:.0f} s){left}"

    def summary(self) -> dict:
        return {"passed": self.passed, "line": self.line(), "earlier": self.earlier, "loads": len(self.loads),
                "failed": [x["what"] for x in self.loads if not x["ok"]], "uncovered": self.uncovered,
                "left_out": self.left_out,
                "errors": self.errors, "seconds": round(self.seconds, 1)}


def spread(items: list, n: int) -> list:
    """n items spread evenly over the list, its first and last kept (all of it when n is 0 or covers it)."""
    if n <= 0 or n >= len(items):
        return list(items)
    if n == 1:
        return [items[0]]
    return [items[round(i * (len(items) - 1) / (n - 1))] for i in range(n)]


def matches(rec: dict, only: list[str]) -> bool:
    return not only or rec["case"] in only or rec["id"] in only


def run(corpora_root: Path, new_corpus: Path | None = None, new_builds: dict | None = None,
        new_release: str | None = None, cache: cs.ReleaseCache | None = None, jobs: int = 4,
        discs: Path | None = None, work: Path | None = None, sample: int = 0, only: list[str] | None = None,
        directions: tuple = ("forward", "backward"), tolerate: int = 0, logs: tuple = ("invariants", "sweep"),
        keep: bool = False, quiet: bool = False, out=sys.stdout, full: int = -1) -> Result:
    """The gate. new_builds: configuration -> state_corpus.Build of the new side; new_corpus: its corpus (made
    in work when None and the backward direction is asked). sample: cases per release, configuration and
    direction (0: all); with full >= 0 only the releases between the first canonical one and the full most
    recent are sampled, those get every case."""
    t0 = time.monotonic()
    result = Result(corpora_root)
    cache = cache or cs.ReleaseCache()
    only = only or []
    new_builds = dict(new_builds or {})
    work = (work or Path("/mnt/Games/scratch/xg-state-compat")).absolute()
    say_lock = threading.Lock()

    def say(text: str) -> None:
        if not quiet:
            with say_lock:
                print(text, file=out, flush=True)

    try:
        everything = cs.corpora(corpora_root)
    except cs.CorpusError as error:
        result.errors.append(str(error))
        return result
    if new_release:
        order = cs.release_order(new_release)
        if new_corpus is not None and (new_corpus / "manifest.json").is_file():
            order = cs.read_manifest(new_corpus)["order"]  # (a stand-in's: the time its corpus was made)
        earlier = [(d, m) for d, m in everything if m["release"] != new_release and m["order"] < order]
    else:
        earlier = everything
    result.earlier = [m["release"] for _, m in earlier]
    if not earlier:
        result.vacuous = (f"no canonical release in {corpora_root} yet" if not everything else
                          f"no canonical release before {new_release} in {corpora_root}")
        result.seconds = time.monotonic() - t0
        return result

    configs_earlier = {c for _, m in earlier for c in m["configs"]}
    for c in sorted(configs_earlier - set(new_builds)):
        result.uncovered.append(f"{c}: no new build of it (forward and backward not run)")
    new_m = None
    made_here = False
    if "backward" in directions:
        wanted = {c: b for c, b in new_builds.items() if c in configs_earlier}
        if new_corpus is None and wanted:
            new_corpus = work / "new-corpus"
            made_here = True
            say(f"making the new build's corpus ({', '.join(wanted)}) in {new_corpus}")
            ids = sorted({c.id for c in cs.all_cases() if c.id in only or any(o.endswith("/" + c.id) for o in only)})
            try:
                # Verified, and the cases the new build does not repeat itself left out (listed): the gate
                # judges loads between builds, a same-build fault is statecheck's.
                made = cs.make_corpus(list(wanted.values()), new_corpus, new_release or "new", ids, jobs, discs,
                                      verify=True, source={"kind": "builds"}, keep_going=True, replace=True,
                                      quiet=quiet)
                result.left_out += [f"{x['id']} (the new build: {x['why'].split(': ', 1)[-1]})" for x in made["failed"]]
            except cs.CorpusError as error:
                result.errors.append(f"the new build's corpus: {error}")
        if new_corpus is not None and not result.errors:
            try:
                new_m = cs.read_manifest(new_corpus)
            except cs.CorpusError as error:
                result.errors.append(str(error))

    tasks = []  # (direction, what, reader Build, corpus dir, record, work dir)
    for i, (d, m) in enumerate(earlier):
        rel = m["release"]
        n = 0 if full >= 0 and (i == 0 or i >= len(earlier) - full) else sample
        if "forward" in directions:
            for config in m["configs"]:
                if config not in new_builds:
                    continue
                recs = [r for r in m["cases"] if r["config"] == config and matches(r, only)]
                for r in spread(recs, n):
                    tasks.append(("forward", f"{rel} {r['id']} -> new", new_builds[config], d, r,
                                  work / "forward" / rel / r["id"]))
        if "backward" in directions and new_m is not None:
            for config, info in m["configs"].items():
                if config not in new_m["configs"]:
                    continue
                try:
                    exe = cache.executable(rel, info["executable"])
                except (cs.CorpusError, KeyError) as error:
                    result.errors.append(f"{rel} {config}: its executable: {error}")
                    continue
                reader = cs.Build(info["region"], info["engine"], exe, None)
                if cs.image_id(exe) != info["build"]:
                    result.errors.append(f"{rel} {config}: {exe} is not the build that wrote its corpus "
                                         f"({cs.image_id(exe)}, the corpus says {info['build']})")
                    continue
                recs = [r for r in new_m["cases"] if r["config"] == config and matches(r, only)]
                for r in spread(recs, n):
                    tasks.append(("backward", f"new {r['id']} -> {rel}", reader, new_corpus, r,
                                  work / "backward" / rel / r["id"]))
            for config in sorted(set(new_m["configs"]) - set(m["configs"])):
                result.uncovered.append(f"{config}: {rel} has no executable of it (backward not run)")

    def one(task):
        direction, what, reader, corpus, rec, wdir = task
        try:
            got = cs.check_load(reader, corpus, rec, wdir, discs, tolerate, logs, keep)
        except Exception as error:  # noqa: BLE001 (a load that could not be run is a failed load)
            got = cs.Loaded(False, [f"could not run: {error}"], [])
        entry = {"direction": direction, "what": what, "case": rec["id"], "ok": got.ok, "problems": got.problems,
                 "notes": got.notes, "how": got.how, "seconds": round(got.seconds, 1), "lines": list(got.lines)}
        say(f"{'PASS' if got.ok else 'FAIL'}  {direction:8}  {what}: "
            + (f"{got.how}, {'+'.join(map(str, got.lines))} lines equal" if got.ok else "; ".join(got.problems))
            + (f" [{'; '.join(got.notes)}]" if got.notes else "") + f" ({got.seconds:.1f} s)")
        return entry

    with concurrent.futures.ThreadPoolExecutor(max(1, jobs)) as pool:
        result.loads = list(pool.map(one, tasks))
    if made_here and result.passed and not keep:
        shutil.rmtree(new_corpus, ignore_errors=True)  # (the gate's own: 100-300 MB a run)
    for d, m in earlier:
        result.left_out += [f"{x['id']} ({m['release']}: {x['why'].split(': ', 1)[-1]})" for x in m.get("failed", [])]
    for u in result.uncovered:
        say(f"not covered: {u}")
    for x in result.left_out:
        say(f"left out: {x}")
    for n in cache.notes:
        say(f"note: {n}")
    result.seconds = time.monotonic() - t0
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--corpora", type=Path, default=cs.CORPORA, help="the canonical releases' corpora")
    ap.add_argument("--new", action="append", default=[], type=Path, metavar="BUILD", help="the new build (one or more)")
    ap.add_argument("--new-corpus", type=Path, help="the new build's corpus, already made")
    ap.add_argument("--release", help="the new side is this release: its corpus is CORPORA/TAG, its executables "
                    "are its manifest's")
    ap.add_argument("--work", type=Path, help="scratch for the runs (default /mnt/Games/scratch/xg-state-compat)")
    ap.add_argument("--cache", type=Path, default=cs.CACHE, help="the releases' tarballs")
    ap.add_argument("--assets-from", type=Path, help="a local stand-in of the GitHub release assets: DIR/<tag>/<asset>")
    ap.add_argument("--offline", action="store_true", help="never download: only what the cache holds")
    ap.add_argument("--jobs", type=int, default=cs.default_jobs())
    ap.add_argument("--sample", type=int, default=0, metavar="N",
                    help="N cases per release, configuration and direction, spread over the corpus (0: all)")
    ap.add_argument("--full", type=int, default=-1, metavar="K",
                    help="with --sample: the first canonical release and the K most recent get every case, only "
                         "those between are sampled")
    ap.add_argument("--case", action="append", default=[], help="only this case (field, us-original/field)")
    ap.add_argument("--forward-only", action="store_true")
    ap.add_argument("--backward-only", action="store_true")
    ap.add_argument("--tolerate", type=int, default=0, metavar="N", help="positions may differ by N units")
    ap.add_argument("--logs", default="invariants,sweep", help="the logs compared (invariants,sweep)")
    ap.add_argument("--discs", type=Path)
    ap.add_argument("--report", type=Path, help="the per-load results as JSON")
    ap.add_argument("--keep", action="store_true", help="keep every run's folder (failed ones are always kept)")
    args = ap.parse_args()
    if args.forward_only and args.backward_only:
        ap.error("--forward-only and --backward-only exclude each other")
    directions = ("forward",) if args.forward_only else ("backward",) if args.backward_only else ("forward", "backward")
    cache = cs.ReleaseCache(args.cache, assets_from=args.assets_from, offline=args.offline)
    try:
        new_builds, new_corpus = {}, args.new_corpus
        if args.release:
            d = args.corpora / args.release
            m = cs.read_manifest(d)
            new_corpus = new_corpus or d
            for config, info in m["configs"].items():
                exe = cache.executable(args.release, info["executable"])
                new_builds[config] = cs.Build(info["region"], info["engine"], exe, None)
        for path in args.new:
            for b in cs.resolve_builds(path):
                if b.config in new_builds:
                    raise cs.CorpusError(f"two new builds of {b.config}")
                new_builds[b.config] = b
        if not new_builds and not args.release:
            ap.error("give --new BUILD or --release TAG")
    except cs.CorpusError as error:
        print(f"state-compat: FAILED: {error}")
        return 1
    result = run(args.corpora, new_corpus, new_builds, args.release, cache, args.jobs, args.discs, args.work,
                 args.sample, args.case, directions, args.tolerate, tuple(x for x in args.logs.split(",") if x),
                 args.keep, full=args.full)
    if args.report:
        args.report.write_text(json.dumps({**result.summary(), "results": result.loads}, indent=1) + "\n")
    print(result.line())
    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
