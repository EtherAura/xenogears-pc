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
   subject-path is the archive step's tarball; the upload sends exactly the
   tarball and its .sha256 with the job's own token.
4. Its scripts, run with bash as the runner runs them: the tag step refuses
   malformed tags (none of them is executed: a canary file stays absent) and
   gives the version for good ones; the archive step, run in a scratch git
   repository of the public tree with a scratch tag and an untracked, a
   game-data-like and a modified file on top, writes
   xenogears-pc-<version>.tar.gz holding exactly the tagged files under
   xenogears-pc-<version>/ (pax comment: the tag's commit) and a .sha256 that
   sha256sum -c accepts; a second run writes the same bytes; a missing tag fails.
5. Mutations of the workflow (no attestation, a missing or extra permission,
   kept credentials, a tag pin, direct interpolation, a third upload, ...)
   are each caught; the candidate file is unchanged afterwards.

Mode "overlay" runs 3 to 5 on the overlay file without assembling the tree; it is
not the acceptance run and says so.

Not checked here (they need GitHub): the real checkout, the attestation (its
OIDC token, signing and storage: the workflow only requests it) and the upload.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
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
JOB_PERMISSIONS = {"contents": "write", "id-token": "write", "attestations": "write"}
TAG_EXPR = "${{ github.event.release.tag_name || inputs.tag }}"
UPLOAD = ["gh", "release", "upload", "$TAG", "$TARBALL", "$CHECKSUM", "--repo", "$GITHUB_REPOSITORY", "--clobber"]
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


def workflow_problems(wf) -> list[str]:
    """Every way the workflow differs from what the release needs (empty: none)."""
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
    missing = [n for n, i in (("tag", v), ("checkout", c), ("archive", a), ("attest", t), ("upload", u)) if i is None]
    if missing:
        return p + [f"steps: no {' '.join(missing)} step"]
    if not (v < c < a < t and a < u):
        p.append("order: the tag is not validated before checkout and archive, or attest/upload precede the archive")
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
            "TARBALL": "${{ steps.archive.outputs.tarball }}", "CHECKSUM": "${{ steps.archive.outputs.checksum }}"}
    if up.get("env") != want:
        p.append(f"upload: env is {up.get('env')}, not {want}")
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
            else:
                raise ValueError(f"expression {alt!r} is not evaluated locally")
            if got:
                return got
        return ""
    return re.sub(r"\$\{\{\s*(.*?)\s*\}\}", lambda m: one(m[1]), str(value))


def run_step(step: dict, ctx: dict, cwd: Path, scratch: Path) -> tuple[int, dict, str]:
    """A run step as the runner runs it (shell bash: bash --noprofile --norc -eo pipefail)."""
    script, output = scratch / "step.sh", scratch / "step.out"
    script.write_text(step["run"])
    output.write_text("")
    env = {"PATH": os.environ["PATH"], "HOME": str(scratch), "LC_ALL": "C", "GITHUB_OUTPUT": str(output)}
    env.update({k: resolve(v, ctx) for k, v in (step.get("env") or {}).items()})
    result = subprocess.run(["bash", "--noprofile", "--norc", "-eo", "pipefail", str(script)],
                            cwd=cwd, env=env, capture_output=True, text=True)
    outputs = dict(line.split("=", 1) for line in output.read_text().splitlines() if "=" in line)
    return result.returncode, outputs, result.stdout + result.stderr


def steps_of(wf: dict) -> dict:
    steps = wf["jobs"]["source"]["steps"]
    return {"tag": next(s for s in steps if s.get("id") == "tag"),
            "archive": next(s for s in steps if s.get("id") == "archive"),
            "attest": next(s for s in steps if str(s.get("uses", "")).startswith("actions/attest-build-provenance@")),
            "upload": next(s for s in steps if "gh release upload" in (s.get("run") or ""))}


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
     lambda w: w["jobs"]["source"]["steps"].__delitem__(3)),
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
     lambda w: w["jobs"]["source"]["steps"][1]["with"].pop("persist-credentials")),
    ("attest pinned by tag", "pin:",
     lambda w: w["jobs"]["source"]["steps"][3].update(uses="actions/attest-build-provenance@v4")),
    ("attest of the checksum", "attest: subject-path",
     lambda w: w["jobs"]["source"]["steps"][3]["with"].update({"subject-path": "${{ steps.archive.outputs.checksum }}"})),
    ("tag interpolated into the script", "interpolation:",
     lambda w: w["jobs"]["source"]["steps"][0].update(run="echo \"tag=${{ inputs.tag }}\" >> \"$GITHUB_OUTPUT\"\n")),
    ("a third upload", "upload: the script",
     lambda w: w["jobs"]["source"]["steps"][4].update(run=w["jobs"]["source"]["steps"][4]["run"].replace(
         '"$CHECKSUM"', '"$CHECKSUM" extra.bin'))),
    ("a secret instead of the job token", "upload: env",
     lambda w: w["jobs"]["source"]["steps"][4]["env"].update(GH_TOKEN="${{ secrets.RELEASE_TOKEN }}")),
    ("release created, not published", "trigger:",
     lambda w: w[True if True in w else "on"]["release"].update(types=["created"])),
    ("checkout before the tag is validated", "order:",
     lambda w: w["jobs"]["source"]["steps"].insert(0, w["jobs"]["source"]["steps"].pop(1))),
]


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

    first = tarball.read_bytes(), sidecar.read_bytes()
    shutil.rmtree(dist)
    rc, _, _ = run_step(steps["archive"], ctx, tree, scratch)
    check(rc == 0 and (tarball.read_bytes(), sidecar.read_bytes()) == first, "a second run writes the same bytes")
    shutil.rmtree(dist)
    missing = {"steps": {"tag": {"tag": "v2099.12.8", "version": "2099.12.8"}}}
    rc, _, _ = run_step(steps["archive"], missing, tree, scratch)
    check(rc != 0 and not (dist / "xenogears-pc-2099.12.8.tar.gz").exists(), "the archive step fails for a missing tag")


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
          "(requested, not verified) and the upload need GitHub and are not checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
