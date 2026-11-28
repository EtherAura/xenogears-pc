#!/usr/bin/env python3
"""Replay every release save-state case with the actual desktop and Android artifacts."""
from __future__ import annotations

import argparse
import concurrent.futures
import fcntl
import functools
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

import state_compat as compat
import state_corpus as cs
from state_runtime import Android, FreeBSD, Windows, external_runner, wine_runner

ABIS = {"android-arm64": "arm64-v8a", "android-x86_64": "x86_64"}
TARGETS = ("windows-x64", "freebsd-amd64", *ABIS)
SCRATCH = Path("/mnt/Games/scratch/xg-platform-gate")


def asset_name(tag: str, target: str) -> str:
    suffix = {"windows-x64": "windows-x64.zip", "freebsd-amd64": "freebsd15-amd64.pkg",
              **{name: f"android-{abi}.apk" for name, abi in ABIS.items()}}
    return f"xenogears-pc-{tag.removeprefix('v')}-{suffix[target]}"


def check_asset(path: Path) -> str:
    sums = path.with_name(path.name + ".sha256")
    if not path.is_file() or not sums.is_file():
        raise cs.CorpusError(f"{path}: the artifact and its .sha256 are required")
    digest = cs.sha256(path)
    if sums.read_text().split() != [digest, path.name]:
        raise cs.CorpusError(f"{path}: SHA-256 mismatch")
    return digest


def check_draft_asset(draft: dict, name: str, digest: str) -> None:
    if draft.get(name) != f"sha256:{digest}":
        raise cs.CorpusError(f"{name}: the tested SHA-256 does not match the draft")


def download(tag: str, names: list[str], dest: Path) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    cmd = ["gh", "release", "download", tag, "--repo", cs.REPO, "--dir", str(dest)]
    for name in names:
        cmd += ["--pattern", name, "--pattern", name + ".sha256"]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    if proc.returncode:
        raise cs.CorpusError(f"{tag}: could not get release artifacts: {proc.stderr.strip()}")


def extract_member(archive: zipfile.ZipFile, member: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    with archive.open(member) as source, dest.open("wb") as output:
        shutil.copyfileobj(source, output)


class Platform:
    def __init__(self, target: str, config: dict, work: Path):
        self.target, self.config, self.work = target, config, work
        self.backend = config["backend"]
        if self.backend == "adb" and target in ABIS:
            self.android = Android(config, ABIS[target])
        elif self.backend == "ssh-freebsd" and target == "freebsd-amd64":
            self.freebsd = FreeBSD(config)
        elif self.backend == "ssh-windows" and target == "windows-x64":
            self.windows = Windows(config)
        elif (self.backend == "wine" and target == "windows-x64") or self.backend == "command":
            if not config.get("command") or not shutil.which(config["command"][0]):
                raise cs.CorpusError(f"{target}: runtime command is unavailable")
        else:
            raise cs.CorpusError(f"{target}: unsupported runtime backend {self.backend}")
        self.jobs = 1 if self.backend in ("adb", "ssh-freebsd", "ssh-windows") else int(config.get("jobs", 1))
        self.extracted: dict[str, list[cs.Build]] = {}

    def builds(self, tag: str, artifact: Path, digest: str) -> list[cs.Build]:
        # Keep the original APK or package in the reader's closure even when two
        # different release versions contain byte-identical native code.
        if digest in self.extracted:
            return self.extracted[digest]
        folder = self.work / "images" / digest
        folder.mkdir(parents=True, exist_ok=True)
        builds = []
        if self.target == "windows-x64":
            with zipfile.ZipFile(artifact) as archive:
                stem = artifact.name.removesuffix(".zip")
                for name in ("SDL3.dll", "release.txt", *(n + ext for n in cs.ENGINES.values()
                              for ext in (".exe", ".xgschema"))):
                    extract_member(archive, f"{stem}/{name}", folder / name)
                for engine, name in cs.ENGINES.items():
                    builds.append(self.build(tag, artifact, digest, "us", engine,
                                             folder / f"{name}.exe", folder / f"{name}.xgschema"))
        elif self.target in ABIS:
            abi = ABIS[self.target]
            with zipfile.ZipFile(artifact) as archive:
                for region in ("us", "jp"):
                    for engine, name in cs.ENGINES.items():
                        base = f"lib{name}-{region}"
                        exe = folder / (base + ".so")
                        schema = folder / (base + ".xgschema")
                        extract_member(archive, f"lib/{abi}/{exe.name}", exe)
                        extract_member(archive, f"assets/schemas/{abi}/{schema.name}", schema)
                        builds.append(self.build(tag, artifact, digest, region, engine, exe, schema))
        else:
            for region in ("us", "jp"):
                for engine, name in cs.ENGINES.items():
                    files = [folder / region / name, folder / region / f"{name}.xgschema"]
                    files[0].parent.mkdir(parents=True, exist_ok=True)
                    for path in files:
                        member = f"/usr/local/libexec/xenogears-pc/{region}/{path.name}"
                        with path.open("wb") as output:
                            proc = subprocess.run(["bsdtar", "-xOf", str(artifact), member],
                                                  stdout=output, stderr=subprocess.PIPE, timeout=120)
                        if proc.returncode or not path.stat().st_size:
                            raise cs.CorpusError(f"{artifact.name}: no {member}")
                    files[0].chmod(0o755)
                    builds.append(self.build(tag, artifact, digest, region, engine, *files))
        self.extracted[digest] = builds
        return builds

    def build(self, tag: str, artifact: Path, digest: str, region: str, engine: str,
              exe: Path, schema: Path) -> cs.Build:
        if cs.schema_version(schema) != region or cs.portable_off(exe):
            raise cs.CorpusError(f"{artifact.name}: {region}-{engine} has no portable state schema")
        if self.backend == "adb":
            runner = functools.partial(self.android.play, artifact, region, engine)
        elif self.backend == "ssh-freebsd":
            runner = functools.partial(self.freebsd.play, exe, schema)
        elif self.backend == "ssh-windows":
            runner = functools.partial(self.windows.play, artifact, digest, exe)
        elif self.backend == "wine":
            runner = wine_runner(exe, self.config)
        else:
            runner = external_runner(self.config["command"], artifact, exe, self.target, region, engine)
        return cs.Build(region, engine, exe, schema, {"executable": {
            "kind": "runtime", "tag": tag, "kept": f"{cs.KEPT}/{artifact.name}", "sha256": digest,
        }}, runner)

    def reader(self, tag: str, info: dict, corpus: Path) -> cs.Build:
        rec = info["executable"]
        artifact = corpus / rec["kept"]
        digest = check_asset(artifact)
        if digest != rec["sha256"]:
            raise cs.CorpusError(f"{artifact}: differs from the artifact that wrote the corpus")
        return next(b for b in self.builds(tag, artifact, digest)
                    if (b.region, b.engine) == (info["region"], info["engine"]))


def capture(platform: Platform, tag: str, artifact: Path, corpus: Path, case_ids: list[str],
            discs: Path | None) -> tuple[list[cs.Build], dict]:
    digest = check_asset(artifact)
    builds = platform.builds(tag, artifact, digest)
    m = cs.make_corpus(builds, corpus, tag, case_ids, platform.jobs, discs, verify=True,
                       failed_dir=platform.work / "failed-captures" / tag)
    for b in builds:
        if m["configs"][b.config]["image_id"] != b.image_id():
            raise cs.CorpusError(f"{artifact.name}: the runtime used a different {b.config} executable")
    keep = [(f"{cs.KEPT}/{f.name}", f) for f in (artifact, artifact.with_name(artifact.name + ".sha256"))]
    cs.keep_in_corpus(corpus, keep, m)
    return builds, m


def file_corpus(corpus: Path, dest: Path, manifest: dict):
    manifest["canonical"] = True
    cs.write_json(corpus / "manifest.json", manifest)
    if not dest.exists():
        corpus.rename(dest)
        return
    previous = Path(tempfile.mkdtemp(prefix=f".superseded-{dest.name}-", dir=dest.parent)) / "corpus"
    dest.rename(previous)
    try:
        corpus.rename(dest)
    except OSError:
        previous.rename(dest)
        raise


def require_result(result: compat.Result, *, allowed_uncovered: bool = False):
    if not allowed_uncovered:
        result.errors.extend(result.uncovered)
    result.errors.extend(result.left_out)
    directions = {x["direction"] for x in result.loads}
    if directions != {"forward", "backward"} and result.passed:
        raise cs.CorpusError("expected runtime compatibility replays in both directions")
    if not result.passed:
        raise cs.CorpusError(result.line() if result.loads or result.errors else "no runtime compatibility loads ran")
    print(result.line(), flush=True)


def validate_corpus(corpus: Path, target: str, cases: list[str]):
    manifest = cs.read_manifest(corpus)
    configs = {c for c in cs.RELEASE_CONFIGS if target != "windows-x64" or c.startswith("us-")}
    expected = {f"{c}/{case.id}" for c in configs for case in cs.find_cases(cases, c.split("-")[0])}
    recorded = [r["id"] for r in manifest["cases"]]
    if (set(manifest["configs"]) != configs or manifest.get("failed") or set(recorded) != expected
            or len(recorded) != len(expected)):
        raise cs.CorpusError(f"{corpus}: incomplete runtime corpus")
    problems = cs.check_corpus(corpus)
    if problems:
        raise cs.CorpusError(f"{corpus}: {problems[0]}")
    return manifest


def run_target(platform: Platform, tag: str, artifact: Path, archive: Path,
               releases: list[tuple[str, dict]], work: Path, assets_from: Path | None, discs: Path | None,
               *, replace_draft: bool = False):
    target = platform.target
    history = archive / target
    history.mkdir(parents=True, exist_ok=True)
    for old, linux_m in releases:
        dest = history / old
        cases = sorted({rec["case"] for rec in linux_m["cases"]})
        if (dest / "manifest.json").is_file():
            validate_corpus(dest, target, cases)
            continue
        # FreeBSD's older standalone package predates portable slots. Its
        # first canonical package is compared to the full Linux history.
        if target == "freebsd-amd64":
            continue
        name = asset_name(old, target)
        if assets_from is None:
            old_assets = work / "assets" / old
            if not (old_assets / name).is_file():
                download(old, [name], old_assets)
        else:
            old_assets = assets_from / old
        with tempfile.TemporaryDirectory(prefix=".historical-", dir=history) as staging:
            corpus = Path(staging) / "corpus"
            _, m = capture(platform, old, old_assets / name, corpus, cases, discs)
            file_corpus(corpus, dest, m)

    digest = check_asset(artifact)
    retained = history / tag
    corpus = history / f".candidate-{tag}-{digest}"
    if retained.exists():
        m = validate_corpus(retained, target, [])
        if {c["executable"]["sha256"] for c in m["configs"].values()} == {digest}:
            corpus = retained
        elif not replace_draft:
            raise cs.CorpusError(f"{retained}: archived artifact differs from the supplied release")
    if (corpus / "manifest.json").is_file():
        m = validate_corpus(corpus, target, [])
        builds = platform.builds(tag, artifact, digest)
        if any(m["configs"][b.config]["executable"]["sha256"] != digest for b in builds):
            raise cs.CorpusError(f"{corpus}: draft artifact changed")
    else:
        builds, m = capture(platform, tag, artifact, corpus, [], discs)
        cs.write_json(corpus / "manifest.json", m)
    return platform, corpus, {b.config: b for b in builds}


def replay_current(writer: Path, readers: dict[str, cs.Build], work: Path, jobs: int, discs: Path | None):
    records = [rec for rec in cs.read_manifest(writer)["cases"] if rec["config"] in readers]
    if not records:
        raise cs.CorpusError(f"{writer}: no compatible region and engine")

    def one(rec):
        try:
            result = cs.check_load(readers[rec["config"]], writer, rec, work / rec["id"], discs)
            return rec["id"], result.problems if not result.ok else []
        except (cs.CorpusError, OSError, subprocess.SubprocessError) as error:
            return rec["id"], [str(error)]

    failures = []
    with concurrent.futures.ThreadPoolExecutor(jobs) as workers:
        for case, problems in workers.map(one, records):
            if problems:
                failures.append(case)
                print(f"FAIL  {writer}: {case}: {'; '.join(problems)}", flush=True)
    return len(records), failures


def replay_matrix(tag: str, runtimes: dict, work: Path, discs: Path | None):
    # Compare every pair that shares a region and engine. Each historical
    # corpus has the platform's own slot, schema and continuation logs.
    totals = {}
    failures = []
    for reader, (platform, candidate, readers) in runtimes.items():
        jobs = platform.jobs if platform else cs.default_jobs()
        for writer, (source, corpus, _) in runtimes.items():
            if source is None and platform is None:
                continue  # Linux already passed the upstream gate.
            pair = f"{writer} to {reader}"
            at = work / "matrix" / reader / writer
            pair_failures = []
            new_loads = 0
            if writer != reader:
                new_loads, current_failures = replay_current(corpus, readers, at / "current", jobs, discs)
                if current_failures:
                    pair_failures.append(f"{len(current_failures)} of {new_loads} current loads failed: "
                                         f"{', '.join(current_failures)} (logs in {at / 'current'})")
            history = corpus.parent
            previous = [(d, m) for d, m in cs.corpora(history) if m["order"] < cs.release_order(tag)]
            if previous:
                # Backward loads run on the writer's runner, forward loads on
                # the reader's. A dedicated mobile runner must stay serial.
                result = compat.run(history, new_corpus=candidate, new_builds=readers, new_release=tag,
                                    work=at / "history", reader_for=source.reader if source else None,
                                    discs=discs, jobs=min(jobs, source.jobs) if source else jobs)
                try:
                    require_result(result, allowed_uncovered="windows-x64" in (writer, reader))
                except cs.CorpusError as error:
                    pair_failures.append(str(error))
                counts = result.summary()
            else:
                counts = {"loads": 0, "earlier": []}
            totals[pair] = {"current": new_loads, **counts, "failures": pair_failures}
            if pair_failures:
                failures.append(pair)
                print(f"FAIL  {pair}: {'; '.join(pair_failures)}", flush=True)
            else:
                print(f"state-platform: {pair}: {new_loads} current and {counts['loads']} historical loads", flush=True)
    if failures:
        work.mkdir(parents=True, exist_ok=True)
        cs.write_json(work / "matrix-failures.json", totals)
        raise cs.CorpusError(f"{len(failures)} platform comparisons failed: {', '.join(failures)}")
    return totals


def replay_memory_cards(runtimes: dict, fixtures: dict, work: Path, discs: Path | None):
    counts = {}
    failures = []
    for target, (_, _, builds) in runtimes.items():
        for cfg, b in builds.items():
            fixture = fixtures[b.region]
            image = Path(fixture["image"]).expanduser().resolve()
            pad = Path(fixture["pad"]).expanduser().resolve()
            if image.stat().st_size != 128 * 1024 or int(fixture["field"]) == 490:
                raise cs.CorpusError(f"{image}: need a raw card that continues past the title screen")
            out = work / "cards" / target / cfg
            cards = out / "cards"
            cards.mkdir(parents=True, exist_ok=True)
            card = cards / "card1.mcd"
            shutil.copy2(image, card)
            env = {"XG_STATE_DIR": str(out / "states"), "XG_PAD_SCRIPT": str(pad),
                   "XG_INVARIANT_LOG": "invariants.log", "XG_SWEEP_LOG": "sweep.log",
                   "XG_DISC2": str(cs.disc_path(b.region, "2", discs))}
            end, expected = int(fixture["quit_at"]), int(fixture["field"])
            code = b.play(out, cs.disc_path(b.region, "1", discs), end, env)
            field = re.compile(rf"^F \d+ \d+ {expected}\s")
            reached = any(field.match(row) for row in cs.log_rows(out / "sweep.log", 0, end))
            if code or not reached or cs.sha256(card) != cs.sha256(image):
                failures.append(f"{target}/{cfg}")
                print(f"FAIL  {target}/{cfg}: memory card did not resume the fixture intact (exit {code})", flush=True)
                continue
            counts[target] = counts.get(target, 0) + 1
            print(f"state-platform: {target}/{cfg} continued from the portable memory card", flush=True)
    if failures:
        raise cs.CorpusError(f"memory card replay failed on {', '.join(failures)}")
    return counts


def background(args) -> int:
    if not re.fullmatch(r"[0-9a-f]{64}", args.background):
        raise cs.CorpusError("expected the draft artifact fingerprint")
    folder = (args.work / "jobs" / args.tag / args.background).resolve()
    folder.mkdir(parents=True, exist_ok=True)
    receipt, log = folder / "result.json", folder / "gate.log"
    unit = f"xg-platform-gate-{args.tag}-{args.background[:16]}"
    status = subprocess.run(["systemctl", "--user", "show", "--value", "--property=ActiveState", unit],
                            capture_output=True, text=True, check=False).stdout.strip()
    if receipt.is_file():
        result = json.loads(receipt.read_text())
        if result["exit"] != 0:
            receipt.rename(folder / f"failed-{receipt.stat().st_mtime_ns}.json")
            if status == "failed":
                subprocess.run(["systemctl", "--user", "reset-failed", unit], check=True)
        if log.is_file():
            print("\n".join(log.read_text(errors="replace").splitlines()[-8:]))
        return result["exit"]
    if status == "failed":
        subprocess.run(["systemctl", "--user", "reset-failed", unit], check=True)
        print(f"state-platform: {args.tag} runtime worker exited without a verdict, log {log}")
        return 1
    if status not in ("active", "activating", "deactivating"):
        forwarded = sys.argv[1:]
        start = forwarded.index("--background")
        worker = [*forwarded[:start], *forwarded[start + 2:], "--result-file", str(receipt)]
        metadata = [f"--setenv={key}={os.environ[key]}" for key in
                    ("DISPLAY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "SSH_AUTH_SOCK", "PATH") if key in os.environ]
        subprocess.run(["systemd-run", "--user", "--quiet", f"--unit={unit}",
                        "--property=Nice=19", "--property=RuntimeMaxSec=48h", "--property=IOSchedulingClass=idle",
                        f"--property=StandardOutput=append:{log}", "--property=StandardError=inherit",
                        f"--working-directory={Path.cwd()}", *metadata,
                        sys.executable, str(Path(__file__).resolve()), *worker], check=True)
    print(f"state-platform: {args.tag} runtime tests are running, log {log}", flush=True)
    return 75


def gate(args) -> int:
    config = json.loads(args.runners.expanduser().read_text())
    for target in TARGETS:
        if target not in config:
            raise cs.CorpusError(f"missing runtime configuration for {target}")
    if (all(config[t]["backend"] == "adb" for t in ABIS)
            and len({config[t]["serial"] for t in ABIS}) != len(ABIS)):
        raise cs.CorpusError("Android ABIs require separate dedicated device serials")
    for region in ("us", "jp"):
        fixture = config["memory_cards"][region]
        if not all(Path(fixture[k]).expanduser().is_file() for k in ("image", "pad")):
            raise cs.CorpusError(f"missing {region.upper()} memory card fixture or pad script")
    linux = args.linux_corpora.resolve()
    current = cs.read_manifest(linux / args.tag)
    if not current["canonical"] or not (current.get("gate") or {}).get("passed"):
        raise cs.CorpusError(f"{args.tag}: its Linux release gate must pass first")
    draft = None
    if args.draft_assets is not None:
        assets = json.loads(args.draft_assets.read_text())
        if assets["tag"] != args.tag:
            raise cs.CorpusError(f"{args.tag}: the draft manifest names another release")
        draft = assets["digests"]
        check_draft_asset(draft, current["source"]["tarball"], current["source"]["sha256"])
    published = cs.ReleaseCache().published_tags() if args.assets_from is None else {
        p.name for p in args.assets_from.iterdir() if p.is_dir()}
    cs.require_release_history(linux, before=args.tag, published=published)
    releases = [(m["release"], m) for _, m in cs.corpora(linux)
                if m["release"] in published and m["order"] < cs.release_order(args.tag)]
    work = Path(tempfile.mkdtemp(prefix=f"{args.tag}-", dir=args.work))
    try:
        platforms = {target: Platform(target, config[target], work / target) for target in TARGETS}
        if args.assets_from is None:
            dest = work / "draft"
            freebsd = config["freebsd-amd64"]
            names = [asset_name(args.tag, t) for t in TARGETS if t != "freebsd-amd64"]
            source = f"xenogears-pc-{args.tag[1:]}.tar.gz"
            if platforms["freebsd-amd64"].backend != "ssh-freebsd":
                raise cs.CorpusError("the release needs a FreeBSD SSH build runner")
            download(args.tag, [*names, source], dest)
            source_digest = check_asset(dest / source)
            if draft is not None:
                check_draft_asset(draft, source, source_digest)
            package = asset_name(args.tag, "freebsd-amd64")
            native_cache = args.work / "freebsd-packages" / args.tag / source_digest
            if not native_cache.exists():
                native_cache.parent.mkdir(parents=True, exist_ok=True)
                at = work / "native-build"
                at.mkdir()
                platforms["freebsd-amd64"].freebsd.build_package(
                    args.tag, dest / source, source_digest, at, freebsd)
                check_asset(at / package)
                at.rename(native_cache)
            check_asset(native_cache / package)
            for name in (package, package + ".sha256"):
                shutil.copy2(native_cache / name, dest / name)
        else:
            dest = args.assets_from.resolve() / args.tag
        for target in TARGETS:
            name = asset_name(args.tag, target)
            digest = check_asset(dest / name)
            if draft is not None and target != "freebsd-amd64":
                check_draft_asset(draft, name, digest)
        cache = cs.ReleaseCache()
        readers = {}
        for key, info in current["configs"].items():
            exe = cache.executable(args.tag, info["executable"], linux / args.tag)
            if cs.image_id(exe) != info["build"]:
                raise cs.CorpusError(f"{args.tag}: wrong Linux executable for {key}")
            readers[key] = cs.Build(info["region"], info["engine"], exe, None)
        runtimes = {"linux-x86_64": (None, linux / args.tag, readers)}
        capture_failures = []
        for target in TARGETS:
            try:
                runtimes[target] = run_target(platforms[target], args.tag, dest / asset_name(args.tag, target),
                                              args.archive.resolve(), releases, work, args.assets_from, args.discs,
                                              replace_draft=draft is not None and args.tag not in published)
            except cs.CorpusError as error:
                capture_failures.append(target)
                print(f"FAIL  {target}: {error}", flush=True)
        failures = [f"could not capture the complete corpus on {', '.join(capture_failures)}"] if capture_failures else []
        try:
            card_results = replay_memory_cards(runtimes, config["memory_cards"], work, args.discs)
        except cs.CorpusError as error:
            failures.append(str(error))
        try:
            results = replay_matrix(args.tag, runtimes, work, args.discs)
        except cs.CorpusError as error:
            failures.append(str(error))
        if failures:
            raise cs.CorpusError("; ".join(failures))
        for target in TARGETS:
            _, corpus, _ = runtimes[target]
            manifest = cs.read_manifest(corpus)
            manifest["matrix"] = results
            manifest["memory_cards"] = card_results
            retained = corpus.parent / args.tag
            if corpus == retained:
                cs.write_json(retained / "manifest.json", manifest)
            else:
                file_corpus(corpus, retained, manifest)
    except Exception:
        print(f"state-platform: failed run files retained in {work}", file=sys.stderr)
        raise
    shutil.rmtree(work)
    print(f"state-platform: {args.tag} all runtime platforms passed")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("tag")
    ap.add_argument("--runners", required=True, type=Path)
    ap.add_argument("--linux-corpora", type=Path, default=cs.CORPORA)
    ap.add_argument("--archive", type=Path, default=cs.CORPORA / ".platforms")
    ap.add_argument("--work", type=Path, default=SCRATCH)
    ap.add_argument("--discs", type=Path)
    ap.add_argument("--assets-from", type=Path, help="test against a local mirror of the release assets")
    ap.add_argument("--draft-assets", type=Path, help="expected digests for the release draft's uploaded assets")
    ap.add_argument("--background", metavar="FINGERPRINT", help="run as a user service keyed to the draft artifacts")
    ap.add_argument("--result-file", type=Path, help=argparse.SUPPRESS)
    args = ap.parse_args()
    try:
        if not cs.TAG.fullmatch(args.tag):
            raise cs.CorpusError(f"invalid release tag: {args.tag}")
        if args.background:
            result = background(args)
        else:
            args.work.mkdir(parents=True, exist_ok=True)
            with (args.work / "runner.lock").open("a") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                result = gate(args)
    except (cs.CorpusError, OSError, ValueError, KeyError, StopIteration, subprocess.SubprocessError,
            zipfile.BadZipFile) as error:
        print(f"state-platform: FAIL: {error}", file=sys.stderr)
        result = 1
    if args.result_file is not None:
        cs.write_json(args.result_file, {"exit": result})
    return result


if __name__ == "__main__":
    sys.exit(main())
