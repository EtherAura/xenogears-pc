#!/usr/bin/env python3
"""Check the Arch PKGBUILD wiring of xenogears-pc's source-release workflow.

    python tools/test_publish_release_arch.py --ver us
    python tools/test_publish_release_arch.py --ver jp

The workflow is tools/publish/repo-port/.github/workflows/release-source.yml;
the renderer and template are tools/publish/repo-port/packaging/ (packaging/
at the public tree's root). The port tree is the same for both versions;
--ver only names the scratch folder, build/pc/release-arch-<ver>/.

1. The wiring, read as GitHub reads it: everything
   tools/test_publish_release_source.py requires still holds (trigger,
   permissions, pins, tag validation, archive, attestation, SBOM, the upload
   of the tarball, its .sha256 and the SBOM); the job uses exactly the three
   pinned actions; one render step (id arch) after the tag, archive and SBOM
   check and before every upload runs the tagged tree's
   packaging/arch_assets.py with the validated tag, the archive step's
   tarball (--tarball, so its bytes are checked) and .sha256, into a fresh
   folder outside the checkout, with no condition or continue-on-error; one
   last upload sends exactly that PKGBUILD with the job's token.
2. The job, run as the runner runs it (bash -eo pipefail, each step's env,
   a failing step ends the job) in a scratch git repository of a minimal
   public-layout fixture (packaging/ copied from the overlay and a labelled
   synthetic file, at synthetic tags): uses-steps are not run (the SBOM
   action's output is a labelled stand-in file) and `gh` is a stand-in on
   PATH that records its arguments and the files' SHA-256 and uploads
   nothing. For two versions (a published release and a dispatched tag) the
   real renderer writes a PKGBUILD whose pkgver, source and sha256sums are the
   version, the release's tarball asset and the SHA-256 of the tarball this
   job made; bash -n accepts it (and makepkg --printsrcinfo, when present);
   the last upload sends exactly it, as PKGBUILD. A changed tarball, a
   checksum of another digest or of another version's tarball, a missing
   tarball or checksum, a stale output folder, malformed tags (through the
   tag step, and straight into the render step) all fail the job before any
   upload, with no PKGBUILD written and none uploaded.
3. Regression: the workflow before this wiring (BASELINE) passes the
   source-release checks, runs the same fixture job to the end, and is still
   caught here (no render step, no PKGBUILD uploaded); mutations of the new
   wiring are each caught; the candidate file is unchanged afterwards.

Every tag, tarball and checksum here is a synthetic test input, not release
evidence. Not checked (they need GitHub or Arch): the real checkout and
upload, the released tarball's digest, makepkg building the package.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = "tools/publish/repo-port/.github/workflows/release-source.yml"
CANDIDATE = ROOT / WORKFLOW
OVERLAY = ROOT / "tools/publish/repo-port/packaging"
BASELINE = "9ef3c7f9"  # the last commit whose workflow has no Arch wiring
URL = "https://github.com/EtherAura/xenogears-pc"
RELEASES = (("v2099.12.7", "2099.12.7", "release_tag"), ("v2098.01.0", "2098.01.0", "input_tag"))
REPO = "synthetic/fixture-repo"
TOKEN = "synthetic-not-a-token"
STAND_IN_SBOM = '{"bomFormat": "CycloneDX", "specVersion": "1.6", "metadata": {"component": ' \
                '{"type": "application", "name": "synthetic test stand-in, not an SBOM"}}}\n'
RENDER_ENV = {"TAG": "${{ steps.tag.outputs.tag }}", "TARBALL": "${{ steps.archive.outputs.tarball }}",
              "CHECKSUM": "${{ steps.archive.outputs.checksum }}"}
RENDER_SCRIPT = ['dir="$RUNNER_TEMP/arch-package"', 'mkdir "$dir"',
                 'python3 -I packaging/arch_assets.py --tag="$TAG" --sha256="$CHECKSUM" --tarball="$TARBALL" '
                 '--out-dir="$dir"',
                 'test -f "$dir/PKGBUILD"', 'echo "pkgbuild=$dir/PKGBUILD" >> "$GITHUB_OUTPUT"']
ARCH_UPLOAD_ENV = {"GH_TOKEN": "${{ github.token }}", "TAG": "${{ steps.tag.outputs.tag }}",
                   "PKGBUILD": "${{ steps.arch.outputs.pkgbuild }}"}
ARCH_UPLOAD = ["gh", "release", "upload", "$TAG", "$PKGBUILD", "--repo", "$GITHUB_REPOSITORY", "--clobber"]
CONTROL = ("if", "continue-on-error", "working-directory", "shell", "uses", "with", "timeout-minutes")

failures: list[str] = []


def check(ok: bool, what: str) -> bool:
    print(f"{'ok  ' if ok else 'FAIL'}  {what}")
    if not ok:
        failures.append(what)
    return ok


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


rs = load("release_source_test", ROOT / "tools/test_publish_release_source.py")
ap = load("arch_package_test", ROOT / "tools/test_arch_package.py")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


# --- the wiring ----------------------------------------------------------------------------------

def is_render(s: dict) -> bool:
    return "arch_assets.py" in (s.get("run") or "")


def is_upload(s: dict) -> bool:
    return "gh release upload" in (s.get("run") or "")


def script_lines(s: dict) -> list[str]:
    return [line.strip() for line in (s.get("run") or "").splitlines() if line.strip()]


def wiring_problems(wf) -> list[str]:
    """Every way the workflow's Arch wiring differs from what the release needs (empty: none)."""
    p = [f"release-source: {x}" for x in rs.workflow_problems(wf)]
    try:
        steps = wf["jobs"]["source"]["steps"]
    except (TypeError, KeyError):
        return p + ["arch: no source job"]
    uses = sorted(str(s["uses"]) for s in steps if "uses" in s)
    if uses != sorted([rs.CHECKOUT, rs.ATTEST, rs.SBOM_ACTION]):
        p.append(f"actions: {uses}, not exactly checkout, attest and sbom at their pins")
    ids = [s["id"] for s in steps if "id" in s]
    if len(ids) != len(set(ids)):
        p.append(f"ids: not unique {ids}")
    render = [i for i, s in enumerate(steps) if is_render(s)]
    # The legacy uploads; the draft route's step (id draft, last, checked by the source-release checks) is apart.
    uploads = [i for i, s in enumerate(steps) if is_upload(s) and s.get("id") != "draft"]
    last = len(steps) - 1 - (1 if steps and steps[-1].get("id") == "draft" else 0)
    if len(render) != 1:
        return p + [f"arch: {len(render)} steps run arch_assets.py, not one render step"]
    r = render[0]
    rs_ = steps[r]
    if rs_.get("id") != "arch":
        p.append(f"arch: the render step's id is {rs_.get('id')!r}, not 'arch'")
    if rs_.get("env") != RENDER_ENV:
        p.append(f"arch: render env is {rs_.get('env')}, not {RENDER_ENV}")
    if script_lines(rs_) != RENDER_SCRIPT:
        p.append(f"arch: render script is {script_lines(rs_)}, not {RENDER_SCRIPT}")
    if [k for k in CONTROL if k in rs_]:
        p.append(f"arch: the render step has {[k for k in CONTROL if k in rs_]}")
    if len(uploads) != 2 or uploads[-1] != last:
        p.append(f"arch upload: steps {uploads} upload, not the release upload then one PKGBUILD upload, last "
                 "(before the draft route's step, if any)")
    else:
        a = steps[uploads[1]]
        lines = script_lines(a)
        if len(lines) != 1 or shlex.split(lines[0]) != ARCH_UPLOAD:
            p.append(f"arch upload: the script is {lines}, not {' '.join(ARCH_UPLOAD)}")
        if a.get("env") != ARCH_UPLOAD_ENV:
            p.append(f"arch upload: env is {a.get('env')}, not {ARCH_UPLOAD_ENV}")
        # Its only allowed control: the legacy route's condition, when the workflow has the route.
        has_route = any(s.get("id") == "route" for s in steps)
        extra = [k for k in CONTROL if k in a and not (k == "if" and has_route and a[k] == rs.LEGACY_IF)]
        if extra:
            p.append(f"arch upload: the step has {extra}")
        if has_route and a.get("if") != rs.LEGACY_IF:
            p.append(f"arch upload: runs on {a.get('if')!r}, not the legacy route {rs.LEGACY_IF}")
    at = {kind: rs.find(steps, rs.KINDS[kind]) for kind in ("tag", "archive", "sbom-check")}
    if None in at.values() or not uploads:
        p.append("arch order: no tag, archive, SBOM check or upload step to order against")
    elif not (at["tag"] < at["archive"] < r and at["sbom-check"] < r < min(uploads)):
        p.append("arch order: not rendered after the tag, archive and SBOM check and before every upload")
    return p


# --- the job, run as the runner runs it ----------------------------------------------------------

def expr(value, ctx: dict) -> str:
    return rs.resolve(re.sub(r"\$\{\{\s*github\.token\s*\}\}", TOKEN, str(value)), ctx)


def fake_gh(box: Path, log: Path) -> Path:
    """A stand-in `gh` that records each call and the SHA-256 of each file named; it uploads nothing."""
    bin_dir = box / "fakebin"
    bin_dir.mkdir()
    gh = bin_dir / "gh"
    gh.write_text(f"#!{sys.executable} -I\n"
                  "# Synthetic stand-in for the GitHub CLI (test_publish_release_arch.py): records, uploads nothing.\n"
                  "import hashlib, json, os, sys\n"
                  "files = {a: hashlib.sha256(open(a, 'rb').read()).hexdigest()\n"
                  "         for a in sys.argv[1:] if os.path.isfile(a)}\n"
                  f"with open({str(log)!r}, 'a') as f:\n"
                  "    f.write(json.dumps({'argv': sys.argv[1:], 'files': files,\n"
                  "                        'token': os.environ.get('GH_TOKEN', '')}) + '\\n')\n")
    gh.chmod(0o755)
    return bin_dir


def run_step(step: dict, ctx: dict, cwd: Path, box: Path) -> tuple[int, dict, str]:
    script, output = box / "step.sh", box / "step.out"
    script.write_text(step["run"])
    output.write_text("")
    env = {"PATH": f"{box / 'fakebin'}{os.pathsep}{os.environ['PATH']}", "HOME": str(box), "LC_ALL": "C",
           "GITHUB_OUTPUT": str(output), "RUNNER_TEMP": str(ctx["runner_temp"]), "GITHUB_REPOSITORY": REPO}
    env.update({k: expr(v, ctx) for k, v in (step.get("env") or {}).items()})
    result = subprocess.run(["bash", "--noprofile", "--norc", "-eo", "pipefail", str(script)],
                            cwd=cwd, env=env, capture_output=True, text=True)
    outputs = dict(line.split("=", 1) for line in output.read_text().splitlines() if "=" in line)
    return result.returncode, outputs, result.stdout + result.stderr


def new_box(scratch: Path) -> tuple[Path, Path, Path]:
    box = scratch / "job"
    shutil.rmtree(box, ignore_errors=True)
    box.mkdir()
    temp = box / "runner-temp"
    temp.mkdir()
    log = box / "gh.log"
    fake_gh(box, log)
    return box, temp, log


def run_job(wf: dict, tree: Path, scratch: Path, tag: str, source: str = "release_tag", inject=None) -> dict:
    """The job's steps in order; a failing step ends it. inject(ctx) runs just before the render step."""
    box, temp, log = new_box(scratch)
    shutil.rmtree(tree / "dist", ignore_errors=True)
    ctx = {"steps": {}, source: tag, "runner_temp": temp}
    steps = wf["jobs"]["source"]["steps"]
    ran, text = [], ""
    for s in steps:
        key = s.get("id") or s.get("name") or s.get("uses")
        if not rs.condition(s, ctx):  # these runs take the legacy route: the draft step is skipped
            ran.append((key, 0))
            continue
        if inject is not None and is_render(s):
            inject(ctx)
        if "uses" in s:
            if str(s["uses"]).startswith("anchore/sbom-action@"):
                (tree / expr(s["with"]["output-file"], ctx)).write_text(STAND_IN_SBOM)
            ran.append((key, 0))
            continue
        rc, outputs, text = run_step(s, ctx, tree, box)
        if s.get("id"):
            ctx["steps"][s["id"]] = outputs
        ran.append((key, rc))
        if rc != 0:
            break
    calls = [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []
    return {"ran": ran, "ok": len(ran) == len(steps) and all(rc == 0 for _, rc in ran), "calls": calls,
            "ctx": ctx, "temp": temp, "box": box, "log": text.strip()}


def pkgbuilds(*dirs: Path) -> list[str]:
    return sorted(str(p) for d in dirs if d.exists() for p in d.rglob("PKGBUILD"))


def fixture_tree(scratch: Path) -> tuple[Path, dict]:
    """A minimal public-layout git repository (synthetic), tagged with every RELEASES tag."""
    tree = scratch / "public"
    (tree / "packaging").mkdir(parents=True)
    for name in ("arch_assets.py", "PKGBUILD.in"):
        shutil.copy2(OVERLAY / name, tree / "packaging" / name)
    (tree / "SYNTHETIC-TEST-FIXTURE.txt").write_text(
        "Synthetic test input for tools/test_publish_release_arch.py: not a release, not release evidence.\n")
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.update(GIT_AUTHOR_NAME="scratch", GIT_AUTHOR_EMAIL="scratch@invalid", GIT_AUTHOR_DATE="@0 +0000",
               GIT_COMMITTER_NAME="scratch", GIT_COMMITTER_EMAIL="scratch@invalid", GIT_COMMITTER_DATE="@0 +0000")
    cfg = ["-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false", "-c", "init.defaultBranch=main"]
    rs.git(*cfg, "init", "-q", cwd=tree, env=env)
    rs.git(*cfg, "add", "-A", ".", cwd=tree, env=env)
    rs.git(*cfg, "commit", "-q", "-m", "synthetic public-layout fixture", cwd=tree, env=env)
    for tag, _, _ in RELEASES:
        rs.git(*cfg, "tag", "-a", "-m", "synthetic test tag, not a release", tag, cwd=tree, env=env)
    return tree, env


def good_jobs(wf: dict, tree: Path, scratch: Path) -> dict[str, str]:
    arch = load("arch_assets_under_test", tree / "packaging/arch_assets.py")
    template = (tree / "packaging/PKGBUILD.in").read_text()
    digests, texts = {}, {}
    for tag, version, source in RELEASES:
        res = run_job(wf, tree, scratch, tag, source)
        if not check(res["ok"], f"{tag} ({source}): every step of the job runs ({res['ran']}; {res['log'] or 'ok'})"):
            continue
        name = f"xenogears-pc-{version}.tar.gz"
        tarball, sidecar = tree / "dist" / name, tree / "dist" / f"{name}.sha256"
        digest = digests[version] = sha256(tarball)
        check(sidecar.read_text() == f"{digest}  {name}\n", f"{tag}: the job's .sha256 is the tarball's ({digest})")
        pk = Path(res["ctx"]["steps"].get("arch", {}).get("pkgbuild", ""))
        out = res["temp"] / "arch-package"
        check(pk == out / "PKGBUILD" and pk.is_file() and sorted(os.listdir(out)) == ["PKGBUILD"],
              f"{tag}: the render step writes exactly {out.relative_to(scratch)}/PKGBUILD and gives it as its output")
        check(not pk.resolve().is_relative_to(tree.resolve()), f"{tag}: the PKGBUILD is outside the checkout")
        if not pk.is_file():
            continue
        calls = res["calls"]
        want = [["release", "upload", tag, f"dist/{name}", f"dist/{name}.sha256", f"dist/xenogears-pc-{version}.cdx.json",
                 "--repo", REPO, "--clobber"],
                ["release", "upload", tag, str(pk), "--repo", REPO, "--clobber"]]
        check([c["argv"] for c in calls] == want,
              f"{tag}: two uploads, the tarball/.sha256/SBOM then exactly the PKGBUILD ({[c['argv'] for c in calls]})")
        if len(calls) == 2:
            check(calls[0]["files"].get(f"dist/{name}") == digest and calls[1]["files"] == {str(pk): sha256(pk)}
                  and Path(calls[1]["argv"][3]).name == "PKGBUILD",
                  f"{tag}: the uploads carry this job's tarball and the rendered file, as the asset PKGBUILD")
            check(all(c["token"] == TOKEN for c in calls), f"{tag}: both uploads use the job's token")
        text = texts[version] = pk.read_text()
        check(text == arch.render(template, version, digest), f"{tag}: the PKGBUILD is the renderer's for this tarball")
        syntax = subprocess.run(["bash", "-n", str(pk)], capture_output=True, text=True)
        check(syntax.returncode == 0, f"{tag}: bash -n accepts the PKGBUILD ({syntax.stderr.strip() or 'ok'})")
        got = ap.sourced(pk)
        check(got.get("pkgver") == [version], f"{tag}: pkgver {got.get('pkgver')}")
        check(got.get("source") == [f"{name}::{URL}/releases/download/{tag}/{name}"],
              f"{tag}: source is the release's tarball asset ({got.get('source')})")
        check(got.get("sha256sums") == [digest], f"{tag}: sha256sums is the job's tarball's SHA-256")
        makepkg = shutil.which("makepkg")
        if makepkg:
            info = subprocess.run([makepkg, "--printsrcinfo"], cwd=pk.parent, capture_output=True, text=True)
            check(info.returncode == 0 and f"pkgver = {version}" in info.stdout and f"sha256sums = {digest}" in info.stdout,
                  f"{tag}: makepkg --printsrcinfo agrees")
        else:
            print(f"note  {tag}: makepkg not on PATH, --printsrcinfo not run")
    check(len(set(texts.values())) == len(RELEASES), "the two versions give different PKGBUILDs")
    return digests


def bad_jobs(wf: dict, tree: Path, scratch: Path, digests: dict[str, str]) -> None:
    (tag, version, _), (_, other, _) = RELEASES[0], RELEASES[1]
    name = f"xenogears-pc-{version}.tar.gz"
    dist = tree / "dist"
    again = {}

    def tarball_changed(ctx):
        again["digest"] = sha256(dist / name)
        with (dist / name).open("ab") as f:
            f.write(b"\0")

    def wrong_digest(ctx):
        (dist / f"{name}.sha256").write_text(f"{digests.get(other, '0' * 64)}  {name}\n")

    def other_version(ctx):
        (dist / f"{name}.sha256").write_text(f"{digests.get(other, '0' * 64)}  xenogears-pc-{other}.tar.gz\n")

    def stale(ctx):
        (ctx["runner_temp"] / "arch-package").mkdir()
        (ctx["runner_temp"] / "arch-package/PKGBUILD").write_text("stale: synthetic leftover\n")

    cases = [("the tarball changed after its checksum", tarball_changed),
             ("a checksum of another digest under this tarball's name", wrong_digest),
             ("the checksum of a different version's tarball", other_version),
             ("the tarball missing", lambda ctx: (dist / name).unlink()),
             ("the checksum missing", lambda ctx: (dist / f"{name}.sha256").unlink()),
             ("a stale output folder", stale)]
    for label, inject in cases:
        res = run_job(wf, tree, scratch, tag, inject=inject)
        last = res["ran"][-1] if res["ran"] else (None, 0)
        check(last[0] == "arch" and last[1] != 0, f"{label}: the render step fails and ends the job ({res['ran'][-1:]})")
        check(not res["calls"], f"{label}: no upload is reached ({res['calls'] or 'none'})")
        check("pkgbuild" not in res["ctx"]["steps"].get("arch", {}), f"{label}: no PKGBUILD output")
        left = pkgbuilds(res["temp"], tree)
        if inject is stale:
            check(left == [str(res["temp"] / "arch-package/PKGBUILD")]
                  and (res["temp"] / "arch-package/PKGBUILD").read_text() == "stale: synthetic leftover\n",
                  f"{label}: the stale file is left as it was, not taken as new")
        else:
            check(not left, f"{label}: no PKGBUILD written anywhere ({left or 'none'})")
    check(again.get("digest") == digests.get(version), "the archive step makes the same tarball bytes again")

    for bad in ["v2026.13.0", "v2099.12.7;touch CANARY", "v2099.12.7$(touch CANARY)", "--help", ""]:
        res = run_job(wf, tree, scratch, bad)
        check(res["ran"][-1][0] == "tag" and res["ran"][-1][1] != 0 and not res["calls"]
              and not (tree / "dist").exists() and not pkgbuilds(res["box"], tree)
              and not list(tree.rglob("CANARY")) and not list(res["box"].rglob("CANARY")),
              f"malformed tag {bad!r}: the tag step refuses it, nothing archived, rendered, run or uploaded")

    # Straight into the render step (a tag step that let it through, or no archive output): still refused.
    render = next((s for s in wf["jobs"]["source"]["steps"] if is_render(s)), None)
    if not check(render is not None, "a render step to run directly"):
        return
    good = run_job(wf, tree, scratch, tag)
    archive = good["ctx"]["steps"].get("archive", {})
    direct = [("tag v2099.13.7", {"tag": "v2099.13.7"}, archive),
              ("tag of another version", {"tag": f"v{other}"}, archive),
              ("tag '--help'", {"tag": "--help"}, archive),
              ("empty tag", {"tag": ""}, archive),
              ("no tarball output", {"tag": tag}, {"checksum": archive.get("checksum", "")}),
              ("no checksum output", {"tag": tag}, {"tarball": archive.get("tarball", "")})]
    for label, tag_out, archive_out in direct:
        box, temp, _ = new_box(scratch)
        ctx = {"steps": {"tag": tag_out, "archive": archive_out}, "runner_temp": temp}
        rc, outputs, _ = run_step(render, ctx, tree, box)
        check(rc != 0 and not outputs and not pkgbuilds(temp, tree),
              f"render step given {label}: refused, no output, no PKGBUILD written")


# --- regression: the pre-change workflow and mutations -------------------------------------------

def render_index(w: dict) -> int:
    return next(i for i, s in enumerate(w["jobs"]["source"]["steps"]) if is_render(s))


def arch_upload(w: dict) -> dict:
    return next(s for s in w["jobs"]["source"]["steps"] if is_upload(s) and "$PKGBUILD" in s["run"])


def move_render_after_first_upload(w: dict) -> None:
    steps = w["jobs"]["source"]["steps"]
    s = steps.pop(render_index(w))
    steps.insert(next(i for i, x in enumerate(steps) if is_upload(x)) + 1, s)


def set_script(s: dict, old: str, new: str) -> None:
    s["run"] = s["run"].replace(old, new)


MUTATIONS = [
    ("no render step", "arch: 0 steps", lambda w: w["jobs"]["source"]["steps"].pop(render_index(w))),
    ("no PKGBUILD upload", "arch upload:", lambda w: w["jobs"]["source"]["steps"].remove(arch_upload(w))),
    ("rendered after the first upload", "arch order:", move_render_after_first_upload),
    ("PKGBUILD uploaded before it is rendered", "arch upload:",
     lambda w: (w["jobs"]["source"]["steps"].remove(arch_upload(w)),
                w["jobs"]["source"]["steps"].insert(render_index(w), copy.deepcopy(ARCH_STEP)))),
    ("render without --tarball", "arch: render script",
     lambda w: set_script(w["jobs"]["source"]["steps"][render_index(w)], ' --tarball="$TARBALL"', "")),
    ("render failure ignored", "arch: the render step has",
     lambda w: w["jobs"]["source"]["steps"][render_index(w)].update({"continue-on-error": True})),
    ("render on a condition", "arch: the render step has",
     lambda w: w["jobs"]["source"]["steps"][render_index(w)].update({"if": "always()"})),
    ("render from the raw event tag", "arch: render env",
     lambda w: w["jobs"]["source"]["steps"][render_index(w)]["env"].update(
         TAG="${{ github.event.release.tag_name }}")),
    ("render from a fixed checksum path", "arch: render env",
     lambda w: w["jobs"]["source"]["steps"][render_index(w)]["env"].update(CHECKSUM="dist/checksum.sha256")),
    ("render into the checkout", "arch: render script",
     lambda w: set_script(w["jobs"]["source"]["steps"][render_index(w)], '"$RUNNER_TEMP/arch-package"', "dist")),
    ("tag interpolated into the render script", "release-source: interpolation",
     lambda w: set_script(w["jobs"]["source"]["steps"][render_index(w)], '"$TAG"', '"${{ inputs.tag }}"')),
    ("PKGBUILD upload by a glob", "arch upload: the script",
     lambda w: set_script(arch_upload(w), '"$PKGBUILD"', '"$RUNNER_TEMP"/arch-package/*')),
    ("PKGBUILD upload of an extra file", "arch upload: the script",
     lambda w: set_script(arch_upload(w), '"$PKGBUILD"', '"$PKGBUILD" extra.bin')),
    ("PKGBUILD upload failure ignored", "arch upload: the step has",
     lambda w: arch_upload(w).update({"continue-on-error": True})),
    ("PKGBUILD upload with a secret", "arch upload: env",
     lambda w: arch_upload(w)["env"].update(GH_TOKEN="${{ secrets.RELEASE_TOKEN }}")),
    ("PKGBUILD added to the existing upload line", "release-source: upload: the script",
     lambda w: set_script(next(s for s in w["jobs"]["source"]["steps"] if is_upload(s)), '"$SBOM"',
                          '"$SBOM" "$PKGBUILD"')),
    ("an extra action", "actions:",
     lambda w: w["jobs"]["source"]["steps"].insert(1, {"uses": "actions/upload-artifact@" + "0" * 40})),
    ("an extra permission", "release-source: permissions: job",
     lambda w: w["jobs"]["source"]["permissions"].update(packages="write")),
]
ARCH_STEP: dict = {}


def regression(wf: dict, tree: Path, scratch: Path) -> None:
    base = yaml.safe_load(rs.git("show", f"{BASELINE}:{WORKFLOW}"))
    # The baseline predates the SBOM action's v0.24.3 pin (5ed00d3b) and the upload route: its pin is brought
    # forward and it is held to the checks of its own time (no route), so only the Arch wiring is missing.
    for s in base["jobs"]["source"]["steps"]:
        if str(s.get("uses", "")).startswith("anchore/sbom-action@"):
            s["uses"] = rs.SBOM_ACTION
    check(not rs.workflow_problems(base, route=False),
          f"baseline {BASELINE}: passes the source-release checks (a failure below is the missing wiring)")
    problems = wiring_problems(base)
    check(any(p.startswith("arch: ") for p in problems), f"baseline {BASELINE}: caught here ({problems})")
    tag, version, _ = RELEASES[0]
    res = run_job(base, tree, scratch, tag)
    argv = [c["argv"] for c in res["calls"]]
    check(res["ok"] and len(argv) == 1 and not any(Path(a).name == "PKGBUILD" for c in argv for a in c),
          f"baseline {BASELINE}: its job runs to the end on the same fixture but uploads no PKGBUILD ({argv})")

    if not check(not wiring_problems(copy.deepcopy(wf)), "the unmutated candidate has no wiring problems"):
        return
    ARCH_STEP.update(copy.deepcopy(arch_upload(wf)))
    for label, expect, mutate in MUTATIONS:
        bad = copy.deepcopy(wf)
        mutate(bad)
        got = wiring_problems(bad)
        check(any(expect in p for p in got), f"mutation caught: {label} ({got})")
    # Why the order matters: rendered after the first upload, a bad tarball is already attached.
    bad = copy.deepcopy(wf)
    move_render_after_first_upload(bad)

    def tarball_changed(ctx):
        with (tree / f"dist/xenogears-pc-{version}.tar.gz").open("ab") as f:
            f.write(b"\0")

    res = run_job(bad, tree, scratch, tag, inject=tarball_changed)
    check(not res["ok"] and len(res["calls"]) == 1,
          f"mutation run: rendering after the first upload lets a failing job attach assets ({len(res['calls'])} upload)")


def main() -> int:
    ap_ = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap_.add_argument("--ver", choices=("us", "jp"), default="us", help="names the scratch folder (the tree is shared)")
    ap_.add_argument("--workflow", type=Path, default=CANDIDATE,
                     help="the workflow file checked (default: the overlay's; another file only to show a "
                          "regression, it is not the acceptance run)")
    args = ap_.parse_args()
    candidate = args.workflow.resolve()
    scratch = ROOT / f"build/pc/release-arch-{args.ver}"
    shutil.rmtree(scratch, ignore_errors=True)
    scratch.mkdir(parents=True)
    if candidate != CANDIDATE:
        print(f"NOTE  checking {candidate}, not the overlay workflow: not the acceptance run")
    for tool in ("git", "bash", "sha256sum"):
        if not check(shutil.which(tool) is not None, f"{tool} on PATH"):
            return report()
    if not check(candidate.is_file(), f"workflow {candidate}"):
        return report()
    before = sha256(candidate)
    wf = yaml.safe_load(candidate.read_text())
    problems = wiring_problems(wf)
    check(not problems, f"candidate: source-release checks and Arch wiring as required ({problems or 'no problems'})")
    publish = load("publish_decomp", ROOT / "tools/publish_decomp.py")
    flagged = [line for line in candidate.read_text().splitlines()
               if publish.LEFTOVER_DATE.search(line) or publish.TIME_OF_DAY.search(line) or publish.FORBIDDEN.search(line)]
    check(not flagged, f"the workflow passes the publisher's date/time/private scan ({flagged or 'clean'})")
    tree, _ = fixture_tree(scratch)
    digests = good_jobs(wf, tree, scratch)
    bad_jobs(wf, tree, scratch, digests)
    regression(wf, tree, scratch)
    check(sha256(candidate) == before, "the candidate workflow is unchanged afterwards")
    return report()


def report() -> int:
    if failures:
        print(f"test_publish_release_arch: {len(failures)} check(s) failed")
        return 1
    print("test_publish_release_arch: all checks pass (synthetic fixtures; the real checkout, upload, released "
          "tarball digest and a makepkg build on Arch need GitHub or Arch and are not checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
