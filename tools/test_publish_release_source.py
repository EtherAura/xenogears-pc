#!/usr/bin/env python3
"""Check xenogears-pc's source-release workflow (.github/workflows/release-source.yml).

    python tools/test_publish_release_source.py --ver us                 # the published tree (acceptance)
    python tools/test_publish_release_source.py --ver jp
    python tools/test_publish_release_source.py --ver us --mode overlay  # the overlay file alone

The workflow is tools/publish/repo-port/.github/workflows/release-source.yml,
laid over the public tree by tools/publish_decomp.py. The port tree is the same
for both versions; --ver only names the scratch folder, build/pc/release-source-<ver>/.

Mode "public" (the default; nothing in it is skipped, a missing tool fails):
1. The published tree: the working tree's tracked changes and this check's two
   files are written to a scratch commit (a copy of the index, a scratch object
   folder with the real one as an alternate; HEAD, the index and every ref are
   unchanged, and are checked to be), and the publisher's own `tree port` makes
   the public tree from it, with its filters and its date, time and private
   text scan, into the scratch folder.
2. The tree carries the workflow unchanged, and no private file (agent files,
   the publisher, docs/PLAN.md, bin/, disc/, work/).
3. The workflow: triggered by a published release (or by hand with a tag),
   no workflow-level permissions, one job with exactly contents, id-token and
   attestations write; the tag is validated first and reaches every script
   only through the environment; actions pinned to the verified commits;
   checkout of the tag without kept credentials; the attestation's
   subject-path is the archive step's tarball; after the archive, a step
   unpacks the tarball outside the checkout and names the SBOM
   dist/xenogears-pc-<version>.cdx.json, the pinned anchore/sbom-action scans
   exactly that unpacked tree with only supported inputs, format cyclonedx-json,
   output-file that name and its own artifact, release-asset and dependency
   snapshot uploads off, a check step fails on a missing, empty or
   non-CycloneDX file; the upload, last, sends exactly the tarball, its .sha256
   and the SBOM with the job's own token.
4. Its scripts, run with bash as the runner runs them: the tag step refuses
   malformed tags (none of them is executed: a canary file stays absent) and
   gives the version for good ones; the archive step, run in a scratch git
   repository of the public tree with a scratch tag and an untracked, a
   game-data-like and a modified file on top, writes
   xenogears-pc-<version>.tar.gz holding exactly the tagged files under
   xenogears-pc-<version>/ (pax comment: the tag's commit) and a .sha256 that
   sha256sum -c accepts; a second run writes the same bytes; a missing tag fails.
   The unpack step, run on that tarball, gives a tree holding exactly the
   tagged bytes (nothing untracked or modified) and the version's .cdx.json
   name, which the SBOM action's path/output-file and the upload resolve to; it
   fails for a missing tarball. The SBOM check step refuses a missing, empty
   or non-CycloneDX file and accepts a minimal hand-written CycloneDX fixture
   (this tests the check only: Syft is not run here).
5. Mutations of the workflow (no attestation, a missing or extra permission,
   kept credentials, a tag pin, direct interpolation, an unrelated extra
   upload, no SBOM step, another format, output or scanned path, the action's
   own uploads left on, ...) are each caught; the candidate file is unchanged
   afterwards.

6. The upload route: dispatch inputs tag and upload_mode (an optional choice of
   legacy and draft, default legacy); a route step after the tag reads only the
   event name and the mode; the legacy uploads run only on route legacy; one
   last draft step runs only on route draft with the job's token and four files,
   calling only gh release view, upload and download, with no overwrite, create,
   edit, publish or ignored failure. The whole job is run (each `if` evaluated)
   in a synthetic tagged fixture with a stateful gh stand-in: published-release
   events and default or legacy dispatches make exactly the legacy uploads, the
   same calls as the pre-route workflow (PRE_DRAFT); an unknown mode stops at the
   route; upload_mode draft adds exactly the four files to an integrator-style
   draft with notes and another uploader's file, leaving those and the draft
   state as they were; no release, a public release, a name already on the
   draft, a failed upload, a release published meanwhile, or a wrong or missing
   downloaded file each fail the draft step, with no fallback. Workflow
   variants that overwrite, create, publish, fall back or carry on after a bad
   check are each caught by those runs.

Mode "overlay" runs 3 to 6 on the overlay file without assembling the tree; it is
not the acceptance run and says so.

Not checked here (they need GitHub): the real checkout, the attestation (its
OIDC token, signing and storage: the workflow only requests it), the SBOM's
generation by Syft and its schema validity (the wiring is checked, not a real
SBOM) and the upload.
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
import tarfile
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
PUBLISHED = ".github/workflows/release-source.yml"
CANDIDATE = ROOT / "tools/publish/repo-port" / PUBLISHED
THIS = Path(__file__).resolve()
CHECKOUT = "actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1"
ATTEST = "actions/attest-build-provenance@4d101475d8b20a2381f78447822ac1eab6504dd8"
SBOM_ACTION = "anchore/sbom-action@66cbf4bc1f1c0d2edc94016e65bc221b6bb0ad6c"  # v0.24.3 (the workflow's pin since 5ed00d3b)
# Inputs confirmed in action.yml at that commit (tracker epic xg-4oq; the action may declare more, but
# the workflow uses none outside this set); an undeclared one is ignored by the runner, with a warning.
SBOM_INPUTS = {"path", "file", "format", "output-file", "artifact-name", "github-token", "syft-version",
               "dependency-snapshot", "upload-artifact", "upload-release-assets"}
SBOM_WITH = {"path": "${{ steps.sbom-source.outputs.tree }}", "format": "cyclonedx-json",
             "output-file": "${{ steps.sbom-source.outputs.sbom }}",
             "upload-artifact": False, "upload-release-assets": False, "dependency-snapshot": False}
JOB_PERMISSIONS = {"contents": "write", "id-token": "write", "attestations": "write"}
TAG_EXPR = "${{ github.event.release.tag_name || inputs.tag }}"
UPLOAD = ["gh", "release", "upload", "$TAG", "$TARBALL", "$CHECKSUM", "$SBOM", "--repo", "$GITHUB_REPOSITORY",
          "--clobber"]
# The upload route: published releases and default dispatches take the legacy upload, only an explicit
# upload_mode draft takes the draft step (the last step).
MODE_INPUT = {"required": False, "type": "choice", "options": ["legacy", "draft"], "default": "legacy"}
ROUTE_ENV = {"EVENT": "${{ github.event_name }}", "MODE": "${{ inputs.upload_mode }}"}
LEGACY_IF = "steps.route.outputs.route == 'legacy'"
DRAFT_IF = "steps.route.outputs.route == 'draft'"
DRAFT_ENV = {"GH_TOKEN": "${{ github.token }}", "TAG": "${{ steps.tag.outputs.tag }}",
             "TARBALL": "${{ steps.archive.outputs.tarball }}", "CHECKSUM": "${{ steps.archive.outputs.checksum }}",
             "SBOM": "${{ steps.sbom-source.outputs.sbom }}", "PKGBUILD": "${{ steps.arch.outputs.pkgbuild }}"}
DRAFT_GH = {("release", "view"), ("release", "upload"), ("release", "download")}
PRE_DRAFT = "5ed00d3b"  # the last commit whose workflow has only the legacy upload route
TAG = "v2099.12.7"  # a scratch tag, in the scratch repository only
VERSION = TAG[1:]
NAME = f"xenogears-pc-{VERSION}"
GOOD_TAGS = {"v2026.11.0": "2026.11.0", "v2027.01.0": "2027.01.0", "v2026.12.10": "2026.12.10"}
BAD_TAGS = ["2026.11.0", "v2026.11", "v2026.13.0", "v2026.00.0", "v2026.1.0", "v2026.11.01", "v26.11.0",
            "v2026.11.0-rc1", "v2026.11.-1", " v2026.11.0", "v2026.11.0\n", "V2026.11.0", "", "refs/tags/v2026.11.0",
            "v2026.11.0;touch CANARY", "v2026.11.0$(touch CANARY)", "$(touch CANARY)", "`touch CANARY`",
            "v2026.11.0\ntouch CANARY", "v2026.11.0 && touch CANARY", "v2026.11.0/../../x"]
PRIVATE = ["CLAUDE.md", "AGENTS.md", ".beads", ".claude", "tools/publish_decomp.py", "tools/publish",
           "docs/PLAN.md", "docs/ASSETS.md", "bin", "disc", "work"]

failures: list[str] = []


def check(ok: bool, what: str) -> bool:
    print(f"{'ok  ' if ok else 'FAIL'}  {what}")
    if not ok:
        failures.append(what)
    return ok


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


# --- the workflow, read as GitHub reads it -------------------------------------------------------

def find(steps: list, pred) -> int | None:
    return next((i for i, s in enumerate(steps) if pred(s)), None)


def route_problems(on, steps: list, v: int, c: int) -> list[str]:
    """The upload route: legacy for published releases and default dispatches, draft only when selected."""
    p: list[str] = []
    inputs = ((on.get("workflow_dispatch") or {}).get("inputs") or {}) if isinstance(on, dict) else {}
    if set(inputs) != {"tag", "upload_mode"}:
        p.append(f"dispatch: inputs {sorted(inputs)}, not tag and upload_mode")
    tag_in = inputs.get("tag") or {}
    if tag_in.get("required") is not True or tag_in.get("type") != "string":
        p.append("dispatch: the tag input is not a required string")
    mode = {k: v_ for k, v_ in (inputs.get("upload_mode") or {}).items() if k != "description"}
    if mode != MODE_INPUT:
        p.append(f"dispatch: upload_mode is {mode}, not {MODE_INPUT}")
    r = find(steps, lambda s: s.get("id") == "route")
    if r is None:
        return p + ["route: no route step"]
    if steps[r].get("env") != ROUTE_ENV:
        p.append(f"route: env is {steps[r].get('env')}, not {ROUTE_ENV}")
    if not v < r < c:
        p.append("route: not chosen after the tag and before checkout")
    uploads = [i for i, s in enumerate(steps) if "gh release upload" in (s.get("run") or "")]
    draft = [i for i in uploads if steps[i].get("id") == "draft"]
    if len(draft) != 1 or draft[0] != len(steps) - 1:
        return p + [f"draft: steps {draft} are the draft step, not exactly one, last"]
    for i, s in enumerate(steps):
        label = s.get("name") or s.get("uses")
        if s.get("continue-on-error") is not None and i in uploads + [r]:
            p.append(f"route: step {label} has continue-on-error")
        if i in uploads and i not in draft:
            if s.get("if") != LEGACY_IF:
                p.append(f"legacy: upload step {label} runs on {s.get('if')!r}, not {LEGACY_IF}")
        elif i not in draft and s.get("if") is not None:
            p.append(f"route: step {label} has a condition")
    d = steps[draft[0]]
    if d.get("if") != DRAFT_IF:
        p.append(f"draft: the draft step runs on {d.get('if')!r}, not {DRAFT_IF}")
    if d.get("env") != DRAFT_ENV:
        p.append(f"draft: env is {d.get('env')}, not {DRAFT_ENV}")
    run = d.get("run") or ""
    calls = set(re.findall(r"(?:^|[\s;&|({])gh\s+([\w-]+)\s+([\w-]+)", run))
    rest = re.sub(r"(?:^|(?<=[\s;&|({]))gh\s+release\s+(view|upload|download)\b", "", run)
    if not calls or calls - DRAFT_GH or re.search(r"(?:^|[\s;&|({])gh\s", rest):
        p.append(f"draft: runs gh other than release view, upload and download ({sorted(calls)})")
    if re.search(r"--clobber|--draft|--skip-existing|\b(create|edit|delete|publish)\b|\|\||set \+e", run):
        p.append("draft: the script may overwrite, create, edit or publish, or ignore a failure")
    return p


def workflow_problems(wf, route: bool = True) -> list[str]:
    """Every way the workflow differs from what the release needs (empty: none). route=False: the checks of
    the workflow before the upload route (an older baseline), without the route and draft checks."""
    p: list[str] = []
    if not isinstance(wf, dict):
        return ["workflow: not a mapping"]
    on = wf.get("on", wf.get(True)) or {}  # YAML 1.1 reads a bare `on` as true
    if not (isinstance(on, dict) and isinstance(on.get("release"), dict)
            and on["release"].get("types") == ["published"]):
        p.append("trigger: not release types [published]")
    if isinstance(on, dict) and set(on) - {"release", "workflow_dispatch"}:
        p.append(f"trigger: other events {sorted(set(on) - {'release', 'workflow_dispatch'})}")
    if wf.get("permissions") != {}:
        p.append(f"permissions: workflow default is {wf.get('permissions')!r}, not {{}}")
    if (wf.get("defaults") or {}).get("run", {}).get("shell") != "bash":
        p.append("defaults: run shell is not bash")
    jobs = wf.get("jobs") or {}
    if list(jobs) != ["source"]:
        return p + [f"jobs: {list(jobs)}, not the one job 'source'"]
    job = jobs["source"]
    if job.get("permissions") != JOB_PERMISSIONS:
        p.append(f"permissions: job has {job.get('permissions')!r}, not {JOB_PERMISSIONS}")
    steps = job.get("steps") or []
    for s in steps:
        uses = s.get("uses")
        if uses is not None and not re.fullmatch(r"[\w.-]+/[\w.-]+@[0-9a-f]{40}", uses):
            p.append(f"pin: {uses} is not pinned to a commit")
        if "${{" in (s.get("run") or ""):
            p.append(f"interpolation: step {s.get('name') or s.get('uses')} puts an expression in its script")
        for key, value in (s.get("env") or {}).items():
            if "github.token" in str(value) and not (key == "GH_TOKEN" and "gh release upload" in (s.get("run") or "")):
                p.append(f"token: {key} given to a step that does not upload")
    if "secrets." in yaml.safe_dump(wf):
        p.append("token: the workflow reads secrets")
    v = find(steps, lambda s: s.get("id") == "tag")
    c = find(steps, lambda s: str(s.get("uses", "")).startswith("actions/checkout@"))
    a = find(steps, lambda s: s.get("id") == "archive")
    t = find(steps, lambda s: str(s.get("uses", "")).startswith("actions/attest-build-provenance@"))
    u = find(steps, lambda s: "gh release upload" in (s.get("run") or ""))
    g = find(steps, lambda s: s.get("id") == "sbom-source")
    b = find(steps, lambda s: str(s.get("uses", "")).startswith("anchore/sbom-action@"))
    k = find(steps, lambda s: "bomFormat" in (s.get("run") or "") and "gh release upload" not in (s.get("run") or ""))
    missing = [n for n, i in (("tag", v), ("checkout", c), ("archive", a), ("attest", t), ("sbom-source", g),
                              ("sbom", b), ("sbom-check", k), ("upload", u)) if i is None]
    if missing:
        return p + [f"steps: no {' '.join(missing)} step"]
    if not (v < c < a < t and a < g < b < k < u and t < u):
        p.append("order: not tag, checkout, archive, then attest and unpack, SBOM, SBOM check, upload last")
    if [s.get("uses") for s in steps if str(s.get("uses", "")).startswith("anchore/sbom-action")] != [SBOM_ACTION]:
        p.append("sbom: not exactly one anchore/sbom-action step")
    sb = steps[b]
    if sb.get("uses") != SBOM_ACTION:
        p.append(f"pin: sbom is {sb.get('uses')}, not {SBOM_ACTION}")
    sw = sb.get("with") or {}
    if set(sw) - SBOM_INPUTS:
        p.append(f"sbom: unsupported input {sorted(set(sw) - SBOM_INPUTS)}")
    if sw != SBOM_WITH:
        p.append(f"sbom: with is {sw}, not {SBOM_WITH}")
    if sb.get("env") or sb.get("if") is not None or sb.get("continue-on-error") is not None:
        p.append("sbom: the step has env, a condition or continue-on-error")
    want_src = {"TARBALL": "${{ steps.archive.outputs.tarball }}", "VERSION": "${{ steps.tag.outputs.version }}"}
    if steps[g].get("env") != want_src:
        p.append(f"sbom-source: env is {steps[g].get('env')}, not {want_src}")
    if steps[k].get("env") != {"SBOM": "${{ steps.sbom-source.outputs.sbom }}"}:
        p.append(f"sbom-check: env is {steps[k].get('env')}, not the SBOM step's output-file")
    for i in (g, k):
        if steps[i].get("if") is not None or steps[i].get("continue-on-error") is not None:
            p.append(f"sbom: step {steps[i].get('name')} has a condition or continue-on-error")
    if (steps[v].get("env") or {}).get("TAG") != TAG_EXPR:
        p.append(f"tag: the tag step's TAG is not {TAG_EXPR}")
    co = steps[c]
    if co.get("uses") != CHECKOUT:
        p.append(f"pin: checkout is {co.get('uses')}, not {CHECKOUT}")
    if (co.get("with") or {}).get("persist-credentials") is not False:
        p.append("checkout: credentials are kept (persist-credentials is not false)")
    if (co.get("with") or {}).get("ref") != "refs/tags/${{ steps.tag.outputs.tag }}":
        p.append("checkout: not the validated tag")
    at = steps[t]
    if at.get("uses") != ATTEST:
        p.append(f"pin: attest is {at.get('uses')}, not {ATTEST}")
    if (at.get("with") or {}).get("subject-path") != "${{ steps.archive.outputs.tarball }}":
        p.append(f"attest: subject-path is {(at.get('with') or {}).get('subject-path')!r}, not the archive's tarball")
    up = steps[u]
    lines = [line for line in (up.get("run") or "").splitlines() if line.strip()]
    if len(lines) != 1 or shlex.split(lines[0]) != UPLOAD:
        p.append(f"upload: the script is {lines}, not {' '.join(UPLOAD)}")
    want = {"GH_TOKEN": "${{ github.token }}", "TAG": "${{ steps.tag.outputs.tag }}",
            "TARBALL": "${{ steps.archive.outputs.tarball }}", "CHECKSUM": "${{ steps.archive.outputs.checksum }}",
            "SBOM": "${{ steps.sbom-source.outputs.sbom }}"}
    if up.get("env") != want:
        p.append(f"upload: env is {up.get('env')}, not {want}")
    if route:
        p += route_problems(on, steps, v, c)
    return p


def resolve(value: str, ctx: dict) -> str:
    """The few expressions the scripts' environment uses; anything else is an error."""
    def one(expr: str) -> str:
        for alt in (a.strip() for a in expr.split("||")):
            m = re.fullmatch(r"steps\.([\w-]+)\.outputs\.([\w-]+)", alt)
            if m:
                got = ctx["steps"].get(m[1], {}).get(m[2], "")
            elif alt == "github.event.release.tag_name":
                got = ctx.get("release_tag", "")
            elif alt == "inputs.tag":
                got = ctx.get("input_tag", "")
            elif alt == "github.event_name":
                got = event_of(ctx)
            elif alt == "inputs.upload_mode":  # a dispatch gets the input's default; other events have no inputs
                got = ctx.get("input_mode", "legacy" if event_of(ctx) == "workflow_dispatch" else "")
            elif alt == "github.token":
                got = ctx.get("token", "")
            else:
                raise ValueError(f"expression {alt!r} is not evaluated locally")
            if got:
                return got
        return ""
    return re.sub(r"\$\{\{\s*(.*?)\s*\}\}", lambda m: one(m[1]), str(value))


def event_of(ctx: dict) -> str:
    return ctx.get("event") or ("release" if ctx.get("release_tag") else "workflow_dispatch")


def condition(step: dict, ctx: dict) -> bool:
    """A step's `if`, for the one form the workflow uses (an output compared with a word)."""
    cond = step.get("if")
    if cond is None:
        return True
    m = re.fullmatch(r"(?:\$\{\{\s*)?steps\.([\w-]+)\.outputs\.([\w-]+) == '([\w-]+)'(?:\s*\}\})?", str(cond).strip())
    if not m:
        raise ValueError(f"condition {cond!r} is not evaluated locally")
    return ctx["steps"].get(m[1], {}).get(m[2], "") == m[3]


def run_step(step: dict, ctx: dict, cwd: Path, scratch: Path) -> tuple[int, dict, str]:
    """A run step as the runner runs it (shell bash: bash --noprofile --norc -eo pipefail)."""
    script, output = scratch / "step.sh", scratch / "step.out"
    script.write_text(step["run"])
    output.write_text("")
    env = {"PATH": os.environ["PATH"], "HOME": str(scratch), "LC_ALL": "C", "GITHUB_OUTPUT": str(output),
           "RUNNER_TEMP": str(ctx.get("runner_temp", scratch / "runner-temp-unset"))}
    if "fakebin" in ctx:  # the job runs of the upload routes: a stand-in gh first on PATH
        env.update(PATH=f"{ctx['fakebin']}{os.pathsep}{os.environ['PATH']}", GITHUB_REPOSITORY=ctx["repo"])
    env.update({k: resolve(v, ctx) for k, v in (step.get("env") or {}).items()})
    result = subprocess.run(["bash", "--noprofile", "--norc", "-eo", "pipefail", str(script)],
                            cwd=cwd, env=env, capture_output=True, text=True)
    outputs = dict(line.split("=", 1) for line in output.read_text().splitlines() if "=" in line)
    return result.returncode, outputs, result.stdout + result.stderr


KINDS = {
    "tag": lambda s: s.get("id") == "tag",
    "route": lambda s: s.get("id") == "route",
    "checkout": lambda s: str(s.get("uses", "")).startswith("actions/checkout@"),
    "archive": lambda s: s.get("id") == "archive",
    "attest": lambda s: str(s.get("uses", "")).startswith("actions/attest-build-provenance@"),
    "sbom-source": lambda s: s.get("id") == "sbom-source",
    "sbom": lambda s: str(s.get("uses", "")).startswith("anchore/sbom-action@"),
    "sbom-check": lambda s: "bomFormat" in (s.get("run") or "") and "gh release upload" not in (s.get("run") or ""),
    "upload": lambda s: "gh release upload" in (s.get("run") or ""),
}


def step(w: dict, kind: str) -> dict:
    """A step of a (mutable) workflow by what it does, not by its index."""
    return next(s for s in w["jobs"]["source"]["steps"] if KINDS[kind](s))


def steps_of(wf: dict) -> dict:
    return {kind: step(wf, kind) for kind in KINDS if kind != "checkout"}


def drop(w: dict, kind: str) -> None:
    w["jobs"]["source"]["steps"].remove(step(w, kind))


def move_before(w: dict, kind: str, other: str) -> None:
    steps = w["jobs"]["source"]["steps"]
    s = step(w, kind)
    steps.remove(s)
    steps.insert(steps.index(step(w, other)), s)


def check_workflow(path: Path, label: str) -> dict:
    wf = yaml.safe_load(path.read_text())
    problems = workflow_problems(wf)
    check(not problems, f"{label}: trigger, permissions, pins, checkout, attestation and upload as required "
                        f"({problems or 'no problems'})")
    return wf


def check_tags(wf: dict, scratch: Path) -> None:
    tag_step = steps_of(wf)["tag"]
    box = scratch / "tags"
    shutil.rmtree(box, ignore_errors=True)
    box.mkdir(parents=True)
    for bad in BAD_TAGS:
        for source in ("release_tag", "input_tag"):
            rc, out, _ = run_step(tag_step, {"steps": {}, source: bad}, box, scratch)
            check(rc != 0 and not out and not any(box.iterdir()),
                  f"tag {bad!r} ({source}) refused, nothing written or executed")
    for good, version in GOOD_TAGS.items():
        rc, out, _ = run_step(tag_step, {"steps": {}, "release_tag": good}, box, scratch)
        check(rc == 0 and out == {"tag": good, "version": version}, f"tag {good} gives version {version}")
    rc, out, _ = run_step(tag_step, {"steps": {}, "input_tag": "v2026.12.10"}, box, scratch)
    check(rc == 0 and out.get("version") == "2026.12.10", "a dispatched tag (inputs.tag) is used when there is no release")


MUTATIONS = [
    ("no attestation step", "steps: no attest",
     lambda w: drop(w, "attest")),
    ("id-token permission dropped", "permissions: job",
     lambda w: w["jobs"]["source"]["permissions"].pop("id-token")),
    ("attestations permission dropped", "permissions: job",
     lambda w: w["jobs"]["source"]["permissions"].pop("attestations")),
    ("contents only read", "permissions: job",
     lambda w: w["jobs"]["source"]["permissions"].update(contents="read")),
    ("an extra permission (packages: write)", "permissions: job",
     lambda w: w["jobs"]["source"]["permissions"].update(packages="write")),
    ("workflow-level contents: write", "permissions: workflow",
     lambda w: w.update(permissions={"contents": "write"})),
    ("checkout keeps credentials", "checkout: credentials",
     lambda w: step(w, "checkout")["with"].pop("persist-credentials")),
    ("attest pinned by tag", "pin:",
     lambda w: step(w, "attest").update(uses="actions/attest-build-provenance@v4")),
    ("attest of the checksum", "attest: subject-path",
     lambda w: step(w, "attest")["with"].update({"subject-path": "${{ steps.archive.outputs.checksum }}"})),
    ("tag interpolated into the script", "interpolation:",
     lambda w: step(w, "tag").update(run="echo \"tag=${{ inputs.tag }}\" >> \"$GITHUB_OUTPUT\"\n")),
    ("an unrelated extra upload", "upload: the script",
     lambda w: step(w, "upload").update(run=step(w, "upload")["run"].replace('"$SBOM"', '"$SBOM" extra.bin'))),
    ("a secret instead of the job token", "upload: env",
     lambda w: step(w, "upload")["env"].update(GH_TOKEN="${{ secrets.RELEASE_TOKEN }}")),
    ("release created, not published", "trigger:",
     lambda w: w[True if True in w else "on"]["release"].update(types=["created"])),
    ("checkout before the tag is validated", "order:",
     lambda w: move_before(w, "checkout", "tag")),
    # The SBOM.
    ("no SBOM generation", "steps: no sbom",
     lambda w: drop(w, "sbom")),
    ("no SBOM check", "steps: no sbom-check",
     lambda w: drop(w, "sbom-check")),
    ("no unpacked tree for the SBOM", "steps: no sbom-source",
     lambda w: drop(w, "sbom-source")),
    ("SBOM pinned by tag", "pin:",
     lambda w: step(w, "sbom").update(uses="anchore/sbom-action@v0")),
    ("SBOM format SPDX", "sbom: with",
     lambda w: step(w, "sbom")["with"].update(format="spdx-json")),
    ("SBOM format left to the default (SPDX)", "sbom: with",
     lambda w: step(w, "sbom")["with"].pop("format")),
    ("SBOM written over the tarball", "sbom: with",
     lambda w: step(w, "sbom")["with"].update({"output-file": "${{ steps.archive.outputs.tarball }}"})),
    ("SBOM output-file a fixed name", "sbom: with",
     lambda w: step(w, "sbom")["with"].update({"output-file": "dist/sbom.cdx.json"})),
    ("SBOM of the workspace, not the tarball's files", "sbom: with",
     lambda w: step(w, "sbom")["with"].update(path=".")),
    ("SBOM of the tarball file instead of the tree", "sbom: with",
     lambda w: (step(w, "sbom")["with"].pop("path"),
                step(w, "sbom")["with"].update(file="${{ steps.archive.outputs.tarball }}"))),
    ("SBOM release-asset upload left on (default true)", "sbom: with",
     lambda w: step(w, "sbom")["with"].pop("upload-release-assets")),
    ("SBOM release-asset upload on", "sbom: with",
     lambda w: step(w, "sbom")["with"].update({"upload-release-assets": True})),
    ("SBOM workflow artifact left on (default true)", "sbom: with",
     lambda w: step(w, "sbom")["with"].pop("upload-artifact")),
    ("SBOM dependency snapshot on", "sbom: with",
     lambda w: step(w, "sbom")["with"].update({"dependency-snapshot": True})),
    ("SBOM given a secret token", "sbom: with",
     lambda w: step(w, "sbom")["with"].update({"github-token": "${{ secrets.RELEASE_TOKEN }}"})),
    ("SBOM input not declared by the action", "sbom: unsupported input",
     lambda w: step(w, "sbom")["with"].update({"upload-assets": False})),
    ("SBOM failure ignored", "sbom: the step",
     lambda w: step(w, "sbom").update({"continue-on-error": True})),
    ("SBOM check reads another file", "sbom-check: env",
     lambda w: step(w, "sbom-check")["env"].update(SBOM="${{ steps.archive.outputs.checksum }}")),
    ("unpack from the workspace version, not the tag's", "sbom-source: env",
     lambda w: step(w, "sbom-source")["env"].update(VERSION="${{ inputs.tag }}")),
    ("SBOM generated before the archive", "order:",
     lambda w: move_before(w, "sbom", "archive")),
    ("upload before the SBOM check", "order:",
     lambda w: move_before(w, "upload", "sbom-check")),
    ("upload without the SBOM", "upload: the script",
     lambda w: step(w, "upload").update(run=step(w, "upload")["run"].replace(' "$SBOM"', ''))),
    ("upload's SBOM is the checksum", "upload: env",
     lambda w: step(w, "upload")["env"].update(SBOM="${{ steps.archive.outputs.checksum }}")),
    # The upload route.
    ("no route step", "route: no route step",
     lambda w: w["jobs"]["source"]["steps"].remove(by_id(w, "route"))),
    ("upload_mode defaults to draft", "dispatch: upload_mode",
     lambda w: dispatch_inputs(w)["upload_mode"].update(default="draft")),
    ("upload_mode a free string", "dispatch: upload_mode",
     lambda w: dispatch_inputs(w)["upload_mode"].update(type="string")),
    ("upload_mode required", "dispatch: upload_mode",
     lambda w: dispatch_inputs(w)["upload_mode"].update(required=True)),
    ("no upload_mode input", "dispatch: inputs",
     lambda w: dispatch_inputs(w).pop("upload_mode")),
    ("route reads a fixed mode", "route: env",
     lambda w: by_id(w, "route")["env"].update(MODE="draft")),
    ("route chosen after checkout", "route: not chosen",
     lambda w: move_before(w, "archive", "route") or move_before(w, "checkout", "archive")),
    ("legacy upload on every route", "legacy: upload step",
     lambda w: step(w, "upload").pop("if")),
    ("legacy upload on the draft route", "legacy: upload step",
     lambda w: step(w, "upload").update({"if": DRAFT_IF})),
    ("draft step on every route", "draft: the draft step runs",
     lambda w: by_id(w, "draft").pop("if")),
    ("draft step not last", "draft: steps",
     lambda w: w["jobs"]["source"]["steps"].append({"name": "late", "run": "true\n"})),
    ("draft upload overwrites", "draft: the script may",
     lambda w: by_id(w, "draft").update(run=by_id(w, "draft")["run"].replace(
         'upload "$TAG" "${files[@]}" --repo "$GITHUB_REPOSITORY"',
         'upload "$TAG" "${files[@]}" --repo "$GITHUB_REPOSITORY" --clobber'))),
    ("draft step publishes the release", "draft: runs gh other",
     lambda w: by_id(w, "draft").update(run=by_id(w, "draft")["run"] + 'gh release edit "$TAG" --draft=false\n')),
    ("draft step creates a release", "draft: runs gh other",
     lambda w: by_id(w, "draft").update(run='gh release create "$TAG" --draft\n' + by_id(w, "draft")["run"])),
    ("draft step through gh api", "draft: runs gh other",
     lambda w: by_id(w, "draft").update(run=by_id(w, "draft")["run"] + 'gh api -X PATCH "repos/x/releases/1"\n')),
    ("draft check failure ignored", "draft: the script may",
     lambda w: by_id(w, "draft").update(run=by_id(w, "draft")["run"].replace(
         'sha256sum --strict -c -- "${names[1]}")', 'sha256sum --strict -c -- "${names[1]}" || true)'))),
    ("draft step failure ignored", "route: step",
     lambda w: by_id(w, "draft").update({"continue-on-error": True})),
    ("draft step with a secret", "draft: env",
     lambda w: by_id(w, "draft")["env"].update(GH_TOKEN="${{ secrets.RELEASE_TOKEN }}")),
    ("draft step without the PKGBUILD", "draft: env",
     lambda w: by_id(w, "draft")["env"].pop("PKGBUILD")),
    ("another step on a condition", "route: step",
     lambda w: step(w, "archive").update({"if": DRAFT_IF})),
]


def by_id(w: dict, sid: str) -> dict:
    return next(s for s in w["jobs"]["source"]["steps"] if s.get("id") == sid)


def dispatch_inputs(w: dict) -> dict:
    return w[True if True in w else "on"]["workflow_dispatch"]["inputs"]


def check_mutations(wf: dict, scratch: Path) -> None:
    check(not workflow_problems(copy.deepcopy(wf)), "the unmutated workflow has no problems")
    for label, expect, mutate in MUTATIONS:
        bad = copy.deepcopy(wf)
        mutate(bad)
        problems = workflow_problems(bad)
        check(any(expect in p for p in problems), f"mutation caught: {label} ({problems})")
    # One on the file itself: the id-token line deleted from a copy.
    text = CANDIDATE.read_text()
    mutated = scratch / "mutated-release-source.yml"
    mutated.write_text("".join(line for line in text.splitlines(keepends=True) if "id-token: write" not in line))
    problems = workflow_problems(yaml.safe_load(mutated.read_text()))
    check(text != mutated.read_text() and any("permissions: job" in p for p in problems),
          f"file mutation caught: id-token line removed ({problems})")


# --- the upload routes, run as the runner runs them ----------------------------------------------

REPO = "synthetic/fixture-repo"
TOKEN = "synthetic-not-a-token"
JOB_TAG = "v2097.03.1"  # a synthetic tag, in the synthetic fixture repository only
JOB_NAME = f"xenogears-pc-{JOB_TAG[1:]}"
NOTES = "Synthetic draft notes written by a stand-in integrator: must stay as they are.\n"
OTHER_ASSET = f"{JOB_NAME}-windows-x64.zip"  # another uploader's file on the same draft
OTHER_BYTES = b"synthetic stand-in for another uploader's asset, not a build\n"
STAND_IN_SBOM = '{"bomFormat": "CycloneDX", "specVersion": "1.6", "metadata": {"component": ' \
                '{"type": "application", "name": "synthetic test stand-in, not an SBOM"}}}\n'

# A stateful stand-in for the GitHub CLI: releases and their assets live in a JSON file; it supports the
# release view/upload/download/create/edit calls as gh does (a view or upload by tag finds a draft too; an
# upload refuses an existing name unless --clobber; a download refuses an existing file), records every call,
# and applies the injected faults. It reaches no network.
FAKE_GH = r'''#!@PYTHON@ -I
# Synthetic stand-in for gh (tools/test_publish_release_source.py): a local JSON state, no network.
import fnmatch, json, os, sys
STATE = @STATE@
with open(STATE) as f:
    st = json.load(f)
argv = sys.argv[1:]
st["calls"].append({"argv": argv, "token": os.environ.get("GH_TOKEN", "")})
faults = st["faults"]

def end(code, msg=""):
    with open(STATE, "w") as f:
        json.dump(st, f)
    if msg:
        sys.stderr.write(msg + "\n")
    sys.exit(code)

if len(argv) < 3 or argv[0] != "release":
    end(1, "stand-in: only gh release subcommands")
sub, rest = argv[1], argv[2:]
opts, flags, pos, i = {}, set(), [], 0
while i < len(rest):
    a = rest[i]
    if a in ("--repo", "--json", "--dir", "--pattern", "--notes", "--draft"):
        if a == "--draft" and (i + 1 == len(rest) or rest[i + 1].startswith("--")):
            flags.add(a); i += 1; continue
        opts.setdefault(a, []).append(rest[i + 1]); i += 2
    elif a.startswith("--draft="):
        opts.setdefault("--draft", []).append(a.split("=", 1)[1]); i += 1
    elif a in ("--clobber",):
        flags.add(a); i += 1
    elif a.startswith("-"):
        end(1, "stand-in: unknown flag " + a)
    else:
        pos.append(a); i += 1
if opts.get("--repo") != [@REPO@] or os.environ.get("GH_TOKEN") != @TOKEN@:
    end(1, "stand-in: wrong repository or token")
tag = pos[0] if pos else ""
rel = st["releases"].get(tag)
if sub == "create":
    if rel is not None:
        end(1, "a release with the same tag name already exists")
    st["releases"][tag] = {"draft": "--draft" in flags, "body": "".join(opts.get("--notes", [])), "assets": {}}
    end(0)
if rel is None:
    end(1, "release not found")
if sub == "view":
    fields = opts.get("--json", [""])[0].split(",")
    full = {"tagName": tag, "isDraft": rel["draft"], "body": rel["body"],
            "assets": [{"name": n, "size": len(bytes.fromhex(h))} for n, h in rel["assets"].items()]}
    if not set(fields) <= set(full):
        end(1, "stand-in: unknown field")
    print(json.dumps({k: full[k] for k in fields}))
    end(0)
if sub == "edit":
    if "--draft" in opts:
        rel["draft"] = opts["--draft"][0] != "false"
    end(0)
if sub == "upload":
    files = pos[1:]
    names = [os.path.basename(p) for p in files]
    if not all(os.path.isfile(p) for p in files):
        end(1, "stand-in: a file to upload is missing")
    if "--clobber" not in flags and set(names) & set(rel["assets"]):
        end(1, "asset under the same name already exists: " + " ".join(sorted(set(names) & set(rel["assets"]))))
    for n, p in zip(names, files):
        if faults.get("upload_fails_after") == len(st["stored"]):
            end(1, "stand-in: upload failed (injected)")
        if n in rel["assets"]:
            st["overwritten"].append(n)
        with open(p, "rb") as f:
            rel["assets"][n] = f.read().hex()
        st["stored"].append(n)
    if faults.get("publish_after_upload"):
        rel["draft"] = False
    end(0)
if sub == "download":
    d = opts.get("--dir", ["."])[0]
    pats = opts.get("--pattern", ["*"])
    hit = [n for n in rel["assets"] if any(fnmatch.fnmatchcase(n, p) for p in pats) and n not in faults.get("hide", [])]
    if not hit:
        end(1, "no assets match the file pattern")
    os.makedirs(d, exist_ok=True)
    for n in hit:
        out = os.path.join(d, n)
        if os.path.exists(out):
            end(1, "stand-in: " + out + " already exists")
        data = bytes.fromhex(rel["assets"][n])
        if n in faults.get("corrupt", []):
            data = data[:-1] + bytes([data[-1] ^ 1])
        if n in faults.get("replace", {}):
            data = faults["replace"][n].encode()
        with open(out, "wb") as f:
            f.write(data)
    end(0)
end(1, "stand-in: gh release " + sub + " is not supported")
'''


def route_fixture(scratch: Path) -> Path:
    """A minimal synthetic public-layout repository (packaging/ from the overlay), tagged JOB_TAG."""
    tree = scratch / "route-fixture"
    shutil.rmtree(tree, ignore_errors=True)
    (tree / "packaging").mkdir(parents=True)
    for name in ("arch_assets.py", "PKGBUILD.in"):
        shutil.copy2(CANDIDATE.parents[2] / "packaging" / name, tree / "packaging" / name)
    (tree / "SYNTHETIC-TEST-FIXTURE.txt").write_text(
        "Synthetic test input for tools/test_publish_release_source.py: not a release.\n")
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.update(GIT_AUTHOR_NAME="scratch", GIT_AUTHOR_EMAIL="scratch@invalid", GIT_AUTHOR_DATE="@0 +0000",
               GIT_COMMITTER_NAME="scratch", GIT_COMMITTER_EMAIL="scratch@invalid", GIT_COMMITTER_DATE="@0 +0000")
    cfg = ["-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false", "-c", "init.defaultBranch=main"]
    git(*cfg, "init", "-q", cwd=tree, env=env)
    git(*cfg, "add", "-A", ".", cwd=tree, env=env)
    git(*cfg, "commit", "-q", "-m", "synthetic fixture", cwd=tree, env=env)
    git(*cfg, "tag", "-a", "-m", "synthetic test tag, not a release", JOB_TAG, cwd=tree, env=env)
    return tree


def release(draft: bool, *extra: tuple[str, bytes]) -> dict:
    return {"draft": draft, "body": NOTES, "assets": {OTHER_ASSET: OTHER_BYTES.hex(), **{n: b.hex() for n, b in extra}}}


def run_route_job(wf: dict, tree: Path, scratch: Path, ctx_in: dict, releases: dict, faults: dict | None = None) -> dict:
    """Every step in order, each `if` evaluated; uses-steps are not run (the SBOM action's output is a labelled
    stand-in file); a failing step ends the job. gh is the stateful stand-in."""
    box = scratch / "route-job"
    shutil.rmtree(box, ignore_errors=True)
    (box / "fakebin").mkdir(parents=True)
    (box / "runner-temp").mkdir()
    shutil.rmtree(tree / "dist", ignore_errors=True)
    state = box / "gh-state.json"
    state.write_text(json.dumps({"releases": copy.deepcopy(releases), "faults": faults or {}, "calls": [],
                                 "stored": [], "overwritten": []}))
    gh = box / "fakebin/gh"
    gh.write_text(FAKE_GH.replace("@PYTHON@", sys.executable).replace("@STATE@", repr(str(state)))
                  .replace("@REPO@", repr(REPO)).replace("@TOKEN@", repr(TOKEN)))
    gh.chmod(0o755)
    ctx = {"steps": {}, "runner_temp": box / "runner-temp", "fakebin": box / "fakebin", "repo": REPO,
           "token": TOKEN, **ctx_in}
    ran, log = [], ""
    for s in wf["jobs"]["source"]["steps"]:
        key = s.get("id") or s.get("name") or s.get("uses")
        if not condition(s, ctx):
            ran.append((key, "skipped"))
            continue
        if "uses" in s:
            if str(s["uses"]).startswith("anchore/sbom-action@"):
                (tree / resolve(s["with"]["output-file"], ctx)).write_text(STAND_IN_SBOM)
            ran.append((key, 0))
            continue
        rc, outputs, log = run_step(s, ctx, tree, box)
        if s.get("id"):
            ctx["steps"][s["id"]] = outputs
        ran.append((key, rc))
        if rc != 0:
            break
    st = json.loads(state.read_text())
    ok = len(ran) == len(wf["jobs"]["source"]["steps"]) and all(rc in (0, "skipped") for _, rc in ran)
    return {"ok": ok, "ran": ran, "ctx": ctx, "state": st, "calls": [c["argv"] for c in st["calls"]],
            "tokens": {c["token"] for c in st["calls"]}, "log": log.strip()}


def job_files(res: dict, tree: Path) -> dict[str, Path]:
    steps = res["ctx"]["steps"]
    return {Path(p).name: (tree / p if not Path(p).is_absolute() else Path(p))
            for p in (steps.get("archive", {}).get("tarball", ""), steps.get("archive", {}).get("checksum", ""),
                      steps.get("sbom-source", {}).get("sbom", ""), steps.get("arch", {}).get("pkgbuild", "")) if p}


def legacy_calls(res: dict) -> list:
    pk = res["ctx"]["steps"].get("arch", {}).get("pkgbuild", "")
    return [["release", "upload", JOB_TAG, f"dist/{JOB_NAME}.tar.gz", f"dist/{JOB_NAME}.tar.gz.sha256",
             f"dist/{JOB_NAME}.cdx.json", "--repo", REPO, "--clobber"],
            ["release", "upload", JOB_TAG, pk, "--repo", REPO, "--clobber"]]


def untouched(res: dict, before: dict) -> list[str]:
    """The draft path's invariants for every run: nothing overwritten, created, edited or published, and the
    notes and the other uploader's file as they were."""
    p, st = [], res["state"]
    if st["overwritten"]:
        p.append(f"overwrote {st['overwritten']}")
    if any("--clobber" in c for c in res["calls"]):
        p.append("a call with --clobber")
    if any(c[:2] not in (["release", "view"], ["release", "upload"], ["release", "download"]) for c in res["calls"]):
        p.append(f"a release call other than view, upload, download ({res['calls']})")
    if set(st["releases"]) != set(before):
        p.append(f"releases {sorted(st['releases'])}, not {sorted(before)}")
    for tag, rel in before.items():
        now = st["releases"].get(tag, {})
        if now.get("body") != rel["body"]:
            p.append(f"{tag}: notes changed")
        for n, h in rel["assets"].items():
            if now.get("assets", {}).get(n) != h:
                p.append(f"{tag}: existing asset {n} changed or removed")
    if res["tokens"] - {TOKEN}:
        p.append("a call without the job's token")
    return p


def route_scenarios(wf: dict, tree: Path, scratch: Path) -> dict[str, list[str]]:
    """Each scenario's problems (empty: as required). Legacy: published release and default dispatches upload
    exactly as before. Draft: only an explicit upload_mode draft, onto an existing draft, adding only its files."""
    out: dict[str, list[str]] = {}
    dispatch = {"input_tag": JOB_TAG}
    legacy = [("published release event", {"release_tag": JOB_TAG}),
              ("dispatch, upload_mode at its default (legacy)", dict(dispatch)),
              ("dispatch, upload_mode legacy given", dict(dispatch, input_mode="legacy")),
              ("dispatch, upload_mode empty", dict(dispatch, input_mode="")),
              ("published release event with a stray draft mode", {"release_tag": JOB_TAG, "input_mode": "draft"})]
    for label, ctx in legacy:
        res = run_route_job(wf, tree, scratch, ctx, {JOB_TAG: release(False)})
        p = [] if res["ok"] else [f"job failed {res['ran'][-1:]} {res['log']}"]
        if res["ctx"]["steps"].get("route") != {"route": "legacy"}:
            p.append(f"route {res['ctx']['steps'].get('route')}")
        if res["calls"] != legacy_calls(res):
            p.append(f"calls {res['calls']}, not the legacy uploads {legacy_calls(res)}")
        if dict(res["ran"]).get("draft") != "skipped":
            p.append("the draft step ran")
        out[f"legacy: {label}"] = p
    for bad in ["Draft", "draft;touch CANARY", "$(touch CANARY)", "legacy\n"]:
        res = run_route_job(wf, tree, scratch, dict(dispatch, input_mode=bad), {JOB_TAG: release(True)})
        p = [] if (not res["ok"] and res["ran"][-1][0] == "route") else [f"ran {res['ran']}"]
        if res["calls"] or list(tree.rglob("CANARY")) or list(scratch.rglob("CANARY")):
            p.append(f"calls {res['calls']} or a canary")
        out[f"unknown upload_mode {bad!r}: refused at the route, nothing uploaded or run"] = p

    draft = dict(dispatch, input_mode="draft")
    ok_before = {JOB_TAG: release(True)}
    res = run_route_job(wf, tree, scratch, draft, ok_before)
    files = job_files(res, tree)
    names = list(files)
    p = [] if res["ok"] else [f"job failed {res['ran'][-1:]} {res['log']}"]
    p += untouched(res, ok_before)
    if res["ctx"]["steps"].get("route") != {"route": "draft"}:
        p.append(f"route {res['ctx']['steps'].get('route')}")
    if any(rc != "skipped" for k, rc in res["ran"] if k in ("Attach the tarball, its checksum and its SBOM to the release",
                                                           "Attach the Arch PKGBUILD to the release")):
        p.append("a legacy upload step ran")
    view = ["release", "view", JOB_TAG, "--repo", REPO, "--json", "tagName,isDraft,assets"]
    upload = ["release", "upload", JOB_TAG, f"dist/{JOB_NAME}.tar.gz", f"dist/{JOB_NAME}.tar.gz.sha256",
              f"dist/{JOB_NAME}.cdx.json", res["ctx"]["steps"].get("arch", {}).get("pkgbuild", ""), "--repo", REPO]
    if res["calls"][:3] != [view, upload, view] or [c[:3] for c in res["calls"][3:]] != [
            ["release", "download", JOB_TAG]] * 4:
        p.append(f"calls {res['calls']}")
    rel = res["state"]["releases"].get(JOB_TAG, {})
    want = {OTHER_ASSET: OTHER_BYTES.hex(), **{n: f.read_bytes().hex() for n, f in files.items() if f.is_file()}}
    if len(files) != 4 or names[3] != "PKGBUILD" or rel.get("assets") != want:
        p.append(f"the draft holds {sorted(rel.get('assets', {}))}, not the other file and this job's four {names}")
    if rel.get("draft") is not True:
        p.append("the release is no longer a draft")
    if res["ctx"]["steps"].get("draft") != {"uploaded": " ".join(names)}:
        p.append(f"draft step output {res['ctx']['steps'].get('draft')}")
    out["draft: an integrator's draft with notes and another file gets exactly this job's four files"] = p

    pk_name = "PKGBUILD"
    sidecar = f"{JOB_NAME}.tar.gz.sha256"
    failing = [
        ("no release for the tag", {"v2097.03.0": release(True)}, None, False),
        ("a published (not draft) release", {JOB_TAG: release(False)}, None, False),
        ("the draft already holds this job's PKGBUILD", {JOB_TAG: release(True, (pk_name, b"other bytes\n"))}, None, False),
        ("the draft already holds this job's checksum", {JOB_TAG: release(True, (sidecar, b"0" * 64 + b"\n"))}, None, False),
        ("the upload fails part-way", ok_before, {"upload_fails_after": 1}, True),
        ("the release is published during the upload", ok_before, {"publish_after_upload": True}, True),
        ("the downloaded tarball differs", ok_before, {"corrupt": [f"{JOB_NAME}.tar.gz"]}, True),
        ("the downloaded checksum is of another tarball", ok_before,
         {"replace": {sidecar: "1" * 64 + f"  {JOB_NAME}.tar.gz\n"}}, True),
        ("the downloaded SBOM is missing", ok_before, {"hide": [f"{JOB_NAME}.cdx.json"]}, True),
        ("the downloaded PKGBUILD differs", ok_before, {"corrupt": [pk_name]}, True),
    ]
    for label, before, faults, reaches_upload in failing:
        res = run_route_job(wf, tree, scratch, draft, before, faults)
        p = []
        if res["ok"] or res["ran"][-1][0] != "draft" or res["ran"][-1][1] in (0, "skipped"):
            p.append(f"the draft step did not fail the job ({res['ran'][-1:]})")
        if "uploaded" in res["ctx"]["steps"].get("draft", {}):
            p.append("the draft step reported success")
        uploads = [c for c in res["calls"] if c[:2] == ["release", "upload"]]
        if len(uploads) != (1 if reaches_upload else 0):
            p.append(f"uploads {uploads}")
        p += untouched(res, before)
        if not reaches_upload and res["state"]["releases"] != before:
            p.append("the release state changed before the upload was refused")
        if (faults or {}).get("publish_after_upload") is None and before.get(JOB_TAG, {}).get("draft") is True \
                and res["state"]["releases"][JOB_TAG]["draft"] is not True:
            p.append("the draft was published")
        out[f"draft fails: {label}"] = p
    return out


def check_routes(wf: dict, scratch: Path) -> None:
    tree = route_fixture(scratch)
    for label, problems in route_scenarios(wf, tree, scratch).items():
        check(not problems, f"{label} ({problems or 'as required'})")

    # Regression: the workflow before the route ran the same legacy job: the same calls, in the same order.
    base = yaml.safe_load(git("show", f"{PRE_DRAFT}:{CANDIDATE.relative_to(ROOT).as_posix()}"))
    check(not workflow_problems(base, route=False) and route_problems(base.get(True, base.get("on")),
                                                                         base["jobs"]["source"]["steps"], 0, 1),
          f"pre-route workflow {PRE_DRAFT}: passes the legacy checks and has no route")
    for label, ctx in (("published release event", {"release_tag": JOB_TAG}),
                       ("dispatch at the default", {"input_tag": JOB_TAG})):
        old = run_route_job(base, tree, scratch, ctx, {JOB_TAG: release(False)})
        new = run_route_job(wf, tree, scratch, ctx, {JOB_TAG: release(False)})
        check(old["ok"] and new["ok"] and old["calls"] == new["calls"] == legacy_calls(new)
              and old["state"]["releases"] == new["state"]["releases"],
              f"legacy {label}: the same uploads and release state as {PRE_DRAFT} ({new['calls']})")

    # The scenarios catch a draft step that overwrites, publishes, falls back or carries on after a bad check.
    d = next(s for s in wf["jobs"]["source"]["steps"] if s.get("id") == "draft")
    upload = 'gh release upload "$TAG" "${files[@]}" --repo "$GITHUB_REPOSITORY"'
    before = 'python3 -I -c "$check" before "$TAG" "$state/before.json" "${names[@]}"'
    after = 'python3 -I -c "$check" after "$TAG" "$state/after.json" "${names[@]}"'
    verify = ['cmp -- "${files[$i]}" "$state/download/${names[$i]}"',
              'sha256sum --strict -c -- "${names[1]}"', 'python3 -I -c "$check" files "$TAG" "$state/download" "${names[@]}"']
    route = next(s for s in wf["jobs"]["source"]["steps"] if s.get("id") == "route")
    behaviour = [
        ("draft upload with --clobber and no name check", d, [(before, "true"), (upload, upload + " --clobber")],
         "draft fails: the draft already holds this job's PKGBUILD"),
        ("draft step publishes the draft afterwards", d,
         [(upload, upload + '\n          gh release edit "$TAG" --repo "$GITHUB_REPOSITORY" --draft=false')],
         "draft: an integrator's draft"),
        ("draft step creates the release when it is absent", d,
         [('gh release view "$TAG" --repo "$GITHUB_REPOSITORY" --json tagName,isDraft,assets > "$state/before.json"',
           'gh release create "$TAG" --repo "$GITHUB_REPOSITORY" --draft || true\n'
           '          gh release view "$TAG" --repo "$GITHUB_REPOSITORY" --json tagName,isDraft,assets > "$state/before.json"')],
         "draft fails: no release for the tag"),
        ("draft step falls back to the legacy upload on a public release", d,
         [(before, before + ' || { gh release upload "$TAG" "${files[@]}" --repo "$GITHUB_REPOSITORY" --clobber; exit 0; }')],
         "draft fails: a published (not draft) release"),
        ("draft step carries on after a checksum failure", d, [(v_, v_ + " || true") for v_ in verify],
         "draft fails: the downloaded checksum is of another tarball"),
        ("draft step skips the check after the upload", d, [(after, "true")],
         "draft fails: the release is published during the upload"),
        ("route sends published releases to the draft step", route,
         [('if [[ "$EVENT" != workflow_dispatch ]]', 'if false')],
         "legacy: published release event with a stray draft mode"),
        ("route defaults to draft", route, [('elif [[ "$MODE" == legacy || -z "$MODE" ]]; then\n  route=legacy',
                                             'elif [[ "$MODE" == legacy || -z "$MODE" ]]; then\n  route=draft')],
         "legacy: dispatch, upload_mode at its default (legacy)"),
    ]
    for label, target, edits, scenario in behaviour:
        bad = copy.deepcopy(wf)
        s = next(x for x in bad["jobs"]["source"]["steps"] if x.get("id") == target.get("id"))
        text = s["run"]
        for old_, new_ in edits:
            text = text.replace(old_, new_)
        if not check(text != s["run"], f"behaviour mutation applies: {label}"):
            continue
        s["run"] = text
        got = next((v_ for k_, v_ in route_scenarios(bad, tree, scratch).items() if k_.startswith(scenario)), None)
        check(bool(got), f"behaviour mutation caught by '{scenario}': {label} ({got})")


# --- the published tree --------------------------------------------------------------------------

def git(*args, cwd: Path = ROOT, env: dict | None = None) -> str:
    return subprocess.run(["git", *args], cwd=cwd, env=env, check=True, capture_output=True, text=True).stdout.strip()


def repo_state() -> tuple:
    index = ROOT / git("rev-parse", "--git-path", "index")
    return git("rev-parse", "HEAD"), sha256(index), git("for-each-ref"), git("stash", "list"), \
        git("status", "--porcelain=v1", "--untracked-files=no")


def assemble(scratch: Path) -> Path | None:
    """The public port tree, made by the publisher from a scratch commit of the working tree."""
    before = repo_state()
    scratch_git = scratch / "git"
    (scratch_git / "objects").mkdir(parents=True)
    shutil.copy2(ROOT / git("rev-parse", "--git-path", "index"), scratch_git / "index")
    objects = (ROOT / git("rev-parse", "--git-path", "objects")).resolve()
    redirect = {"GIT_OBJECT_DIRECTORY": str(scratch_git / "objects"), "GIT_ALTERNATE_OBJECT_DIRECTORIES": str(objects)}
    env = dict(os.environ, GIT_INDEX_FILE=str(scratch_git / "index"), **redirect,
               GIT_AUTHOR_NAME="scratch", GIT_AUTHOR_EMAIL="scratch@invalid",
               GIT_COMMITTER_NAME="scratch", GIT_COMMITTER_EMAIL="scratch@invalid")
    git("add", "-u", env=env)
    git("add", "--", str(CANDIDATE.relative_to(ROOT)), str(THIS.relative_to(ROOT)), env=env)
    commit = git("commit-tree", git("write-tree", env=env), "-p", "HEAD",
                 "-m", "scratch: the working tree, for test_publish_release_source.py", env=env)
    print(f"scratch commit {commit} (objects in {scratch_git.relative_to(ROOT)})")
    real = subprocess.run(["git", "cat-file", "-e", commit], cwd=ROOT, capture_output=True)
    check(real.returncode != 0, "the scratch commit is not in the real object store")
    listed = git("ls-tree", "-r", "--name-only", commit, env=env).splitlines()
    check(str(CANDIDATE.relative_to(ROOT)) in listed, "the scratch commit carries the candidate workflow")

    spec = importlib.util.spec_from_file_location("publish_decomp", ROOT / "tools/publish_decomp.py")
    publish = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(publish)
    publish.RELEASE = scratch / "release"
    saved = {k: os.environ.get(k) for k in redirect}
    os.environ.update(redirect)  # the publisher's git calls inherit the environment
    try:
        publish.cmd_tree(argparse.Namespace(target="port", rev=commit))
        made = True
    except SystemExit as stop:
        made = check(False, f"the publisher's tree port refused the tree: {stop}")
    finally:
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
    check(repo_state() == before, "HEAD, the index, every ref, the stash and the tracked files are unchanged")
    if made:
        check(True, "the publisher's tree port made the tree (its filters and its date/time/private scan pass)")
    tree = scratch / "release/port/tree"
    return tree if made and tree.is_dir() else None


def snapshot(tree: Path) -> dict:
    files = {}
    for path in tree.rglob("*"):
        rel = path.relative_to(tree).as_posix()
        if rel == ".git" or rel.startswith(".git/"):
            continue
        if path.is_symlink():
            files[rel] = ("link", os.readlink(path))
        elif path.is_file():
            files[rel] = ("file", sha256(path))
    return files


def exercise(wf: dict, tree: Path, scratch: Path) -> None:
    """The tag and archive steps in a scratch repository of the public tree, at a scratch tag."""
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.update(GIT_AUTHOR_NAME="scratch", GIT_AUTHOR_EMAIL="scratch@invalid",
               GIT_COMMITTER_NAME="scratch", GIT_COMMITTER_EMAIL="scratch@invalid")
    cfg = ["-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false", "-c", "init.defaultBranch=main"]
    expected = snapshot(tree)
    git(*cfg, "init", "-q", cwd=tree, env=env)
    git(*cfg, "add", "-A", "-f", ".", cwd=tree, env=env)
    git(*cfg, "commit", "-q", "-m", "scratch public tree", cwd=tree, env=env)
    git(*cfg, "tag", "-a", "-m", "scratch tag", TAG, cwd=tree, env=env)
    commit = git("rev-parse", f"{TAG}^{{commit}}", cwd=tree, env=env)
    # What a dirty checkout could add: none of it may reach the archive.
    (tree / "UNTRACKED-PRIVATE.txt").write_text("untracked fixture: must not be archived\n")
    (tree / "disc/us").mkdir(parents=True, exist_ok=True)
    (tree / "disc/us/FIXTURE.BIN").write_text("game-data-like fixture path (text): must not be archived\n")
    readme = tree / "README.md"
    readme.write_text(readme.read_text() + "\nmodified after the tag: must not be archived\n")

    steps = steps_of(wf)
    ctx = {"steps": {}, "release_tag": TAG}
    rc, ctx["steps"]["tag"], log = run_step(steps["tag"], ctx, tree, scratch)
    check(rc == 0, f"tag step accepts {TAG} ({log.strip() or ctx['steps']['tag']})")
    rc, ctx["steps"]["archive"], log = run_step(steps["archive"], ctx, tree, scratch)
    if not check(rc == 0, f"archive step ran ({log.strip() or 'no output'})"):
        return
    dist = tree / "dist"
    tarball, sidecar = dist / f"{NAME}.tar.gz", dist / f"{NAME}.tar.gz.sha256"
    check(sorted(p.name for p in dist.iterdir()) == sorted([tarball.name, sidecar.name]),
          f"dist holds exactly {tarball.name} and {sidecar.name}")
    subject = tree / resolve(steps["attest"]["with"]["subject-path"], ctx)
    check(subject.resolve() == tarball.resolve() and tarball.is_file(),
          f"the attestation's subject-path resolves to the real tarball ({subject.relative_to(tree)})")
    uploads = [tree / resolve(steps["upload"]["env"][k], ctx) for k in ("TARBALL", "CHECKSUM")]
    check([u.resolve() for u in uploads] == [tarball.resolve(), sidecar.resolve()] and all(u.is_file() for u in uploads),
          "the upload's TARBALL and CHECKSUM resolve to the two files written")
    check(resolve(steps["upload"]["env"]["TAG"], ctx) == TAG, "the upload names the validated tag")

    digest = sha256(tarball)
    check(sidecar.read_text() == f"{digest}  {tarball.name}\n", f"{sidecar.name}: {digest}  {tarball.name}")
    check(shutil.which("sha256sum") is not None
          and subprocess.run(["sha256sum", "-c", sidecar.name], cwd=dist, capture_output=True).returncode == 0,
          "sha256sum -c accepts the sidecar against the tarball's bytes")

    with tarfile.open(tarball, "r:gz") as tar:
        check(tar.pax_headers.get("comment") == commit, f"the archive's pax comment is the tag's commit {commit}")
        members = tar.getmembers()
        names = [m.name for m in members]
        check(all(n == NAME or n.startswith(NAME + "/") for n in names), f"every entry is under {NAME}/")
        got = {}
        for m in members:
            rel = m.name[len(NAME) + 1:]
            if m.issym():
                got[rel] = ("link", m.linkname)
            elif m.isfile():
                got[rel] = ("file", hashlib.sha256(tar.extractfile(m).read()).hexdigest())
    tagged = set(git("ls-tree", "-r", "--name-only", TAG, cwd=tree, env=env).splitlines())
    check(set(got) == tagged == set(expected), f"the archive holds exactly the {len(tagged)} tagged files")
    check(got == expected, "every archived file has the published tree's bytes (README.md as tagged, not as modified)")
    leaked = sorted(n for n in got if n in ("UNTRACKED-PRIVATE.txt",) or n.startswith(("disc/", "dist/", ".git/")))
    check(not leaked, f"no untracked, game-data path, dist or .git entry ({leaked or 'none'})")

    exercise_sbom(steps, ctx, tree, scratch, expected, tarball, sidecar)

    first = tarball.read_bytes(), sidecar.read_bytes()
    shutil.rmtree(dist)
    rc, _, _ = run_step(steps["archive"], ctx, tree, scratch)
    check(rc == 0 and (tarball.read_bytes(), sidecar.read_bytes()) == first, "a second run writes the same bytes")
    shutil.rmtree(dist)
    missing = {"steps": {"tag": {"tag": "v2099.12.8", "version": "2099.12.8"}}}
    rc, _, _ = run_step(steps["archive"], missing, tree, scratch)
    check(rc != 0 and not (dist / "xenogears-pc-2099.12.8.tar.gz").exists(), "the archive step fails for a missing tag")


def exercise_sbom(steps: dict, ctx: dict, tree: Path, scratch: Path, expected: dict,
                  tarball: Path, sidecar: Path) -> None:
    """The unpack and SBOM check steps; Syft itself (the action) is not run here."""
    temp = scratch / "runner-temp"
    shutil.rmtree(temp, ignore_errors=True)
    temp.mkdir()
    ctx["runner_temp"] = temp
    rc, ctx["steps"]["sbom-source"], log = run_step(steps["sbom-source"], ctx, tree, scratch)
    if not check(rc == 0, f"unpack step ran ({log.strip() or ctx['steps']['sbom-source']})"):
        return
    unpacked, sbom = temp / "sbom-source" / NAME, tree / "dist" / f"{NAME}.cdx.json"
    check(ctx["steps"]["sbom-source"] == {"tree": str(unpacked), "sbom": f"dist/{NAME}.cdx.json"},
          f"unpack step gives tree {unpacked.relative_to(scratch)} and sbom dist/{NAME}.cdx.json")
    check(not unpacked.resolve().is_relative_to(tree.resolve()), "the unpacked tree is outside the checkout")
    check(snapshot(unpacked) == expected and not (unpacked / "UNTRACKED-PRIVATE.txt").exists()
          and not (unpacked / "disc").exists(),
          f"the unpacked tree holds exactly the {len(expected)} tagged files with their tagged bytes")
    check(sorted(p.name for p in sbom.parent.iterdir()) == sorted([tarball.name, sidecar.name]),
          "unpacking writes nothing into dist (the SBOM is the action's to write)")
    w = steps["sbom"]["with"]
    check(Path(resolve(w["path"], ctx)) == unpacked, "the SBOM action scans the unpacked tagged tree")
    check(tree / resolve(w["output-file"], ctx) == sbom and sbom.name != tarball.name,
          f"the SBOM action writes {sbom.name}, beside the tarball, not over it")
    up = steps["upload"]["env"]
    check([tree / resolve(up[k], ctx) for k in ("TARBALL", "CHECKSUM", "SBOM")] == [tarball, sidecar, sbom],
          "the upload's TARBALL, CHECKSUM and SBOM are the tarball, its .sha256 and the SBOM's output-file")
    check(tree / resolve(steps["sbom-check"]["env"]["SBOM"], ctx) == sbom, "the SBOM check reads the output-file")

    # The check step, on stand-in files (no SBOM is generated here; these only exercise the check).
    cases = [("missing", None, False), ("empty", "", False), ("not JSON", "not json\n", False),
             ("SPDX, not CycloneDX", '{"spdxVersion": "SPDX-2.3"}\n', False),
             ("a JSON list", '["bomFormat"]\n', False),
             ("CycloneDX without specVersion", '{"bomFormat": "CycloneDX"}\n', False),
             ("minimal hand-written CycloneDX fixture", '{"bomFormat": "CycloneDX", "specVersion": "1.6"}\n', True)]
    for label, text, ok in cases:
        sbom.unlink(missing_ok=True)
        if text is not None:
            sbom.write_text(text)
        rc, _, _ = run_step(steps["sbom-check"], ctx, tree, scratch)
        check((rc == 0) == ok, f"SBOM check {'accepts' if ok else 'refuses'} {label}")
    sbom.unlink(missing_ok=True)

    shutil.rmtree(temp)
    temp.mkdir()
    gone = dict(ctx, steps=dict(ctx["steps"], archive={"tarball": "dist/absent.tar.gz"}))
    rc, out, _ = run_step(steps["sbom-source"], gone, tree, scratch)
    check(rc != 0 and not out, "the unpack step fails for a missing tarball")
    shutil.rmtree(temp)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ver", choices=("us", "jp"), default="us", help="names the scratch folder (the tree is shared)")
    ap.add_argument("--mode", choices=("public", "overlay"), default="public",
                    help="public: assemble the published tree (acceptance); overlay: the overlay file alone")
    args = ap.parse_args()
    scratch = ROOT / f"build/pc/release-source-{args.ver}"
    shutil.rmtree(scratch, ignore_errors=True)
    scratch.mkdir(parents=True)
    print(f"MODE {args.mode} (--ver {args.ver}: the port tree is the same for US and JP)")
    for tool in ("git", "bash", "sha256sum"):
        if not check(shutil.which(tool) is not None, f"{tool} on PATH"):
            return report(args.mode)
    if not check(CANDIDATE.is_file(), f"candidate {CANDIDATE.relative_to(ROOT)}"):
        return report(args.mode)
    candidate = sha256(CANDIDATE)

    if args.mode == "public":
        tree = assemble(scratch)
        if tree is None:
            return report(args.mode)
        published = tree / PUBLISHED
        check(published.is_file() and published.read_bytes() == CANDIDATE.read_bytes(),
              f"the public tree carries {PUBLISHED}, unchanged by the publisher")
        present = [p for p in PRIVATE if (tree / p).exists()]
        check(not present, f"no private path in the public tree ({present or 'none'})")
        wf = check_workflow(published, "public tree workflow")
    else:
        wf = check_workflow(CANDIDATE, "overlay workflow")
    if failures:
        return report(args.mode)
    check_tags(wf, scratch)
    if args.mode == "public":
        exercise(wf, tree, scratch)
    check_routes(wf, scratch)
    check_mutations(wf, scratch)
    check(sha256(CANDIDATE) == candidate, "the candidate workflow is unchanged after the mutations")
    return report(args.mode)


def report(mode: str) -> int:
    if failures:
        print(f"test_publish_release_source: {len(failures)} check(s) failed")
        return 1
    scope = "published tree assembled by the publisher" if mode == "public" else \
        "overlay file only, the published tree was NOT assembled (not the acceptance run)"
    print(f"test_publish_release_source: all checks pass ({scope}; the real checkout, the attestation "
          "(requested, not verified), the SBOM's generation by Syft and its schema validity (wiring checked, "
          "no real SBOM made) and the upload need GitHub and are not checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
