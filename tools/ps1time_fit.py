#!/usr/bin/env python3
"""The PS1 time model's fit (xg-wv9t): how long the PS1 took for the game's work.

Input: PS1 time logs (port/src/system/ps1time.c, XG_PS1TIME_LOG) written while the
port replayed a Beetle PSX VSync log (XG_SLOWDOWN, tools/beetle.py --vsync-log), so
the clock on their VSync lines is the PS1's. The work between two VSync calls is a
segment: its counters (GTE commands, safe points) and its PS1 time.

    python tools/ps1time_fit.py segments LOG...      # the segments, a summary per mode
    python tools/ps1time_fit.py fit LOG... [--test LOG...] [--json OUT]

`fit` fits CPU time per segment as a per-mode constant plus a cost per GTE command
group and per safe point (a platform call: LIBGPU, LIBCD, libc), by least squares with the costs kept
at zero or above, and reports the error on the logs given (and on --test logs).
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter, defaultdict

import numpy as np

# GTE command groups (psx-spx "GTE Command Summary" numbers).
GTE_GROUPS = {
    "rtps": [0x01], "rtpt": [0x30], "nclip": [0x06], "avsz": [0x2D, 0x2E], "mvmva": [0x12],
    "light1": [0x13, 0x1B, 0x1E], "light3": [0x16, 0x20, 0x3F],
    "colour": [0x10, 0x11, 0x14, 0x1C, 0x29, 0x2A], "misc": [0x0C, 0x28, 0x3D, 0x3E],
}
GROUP_OF = {c: g for g, cs in GTE_GROUPS.items() for c in cs}
GPU_KINDS = ["poly_flat", "poly_gouraud", "poly_tex4", "poly_tex8", "poly_tex15", "rect_flat", "rect_tex4",
             "rect_tex8", "rect_tex15", "line", "fill", "copy", "load"]


def parse_gte(field: str) -> Counter:
    c = Counter()
    if field != "-":
        for item in field.split(","):
            cmd, n = item.split(":")
            c[GROUP_OF.get(int(cmd, 16), "misc")] += int(n)
    return c


def parse_gpu(field: str) -> dict:
    out = {"count": np.zeros(len(GPU_KINDS)), "pixels": np.zeros(len(GPU_KINDS)), "semi": 0.0, "shaded": 0.0}
    if field != "-":
        for item in field.split(","):
            parts = item.split(":")
            if parts[0] == "s":
                out["semi"] = float(parts[1])
            elif parts[0] == "m":
                out["shaded"] = float(parts[1])
            else:
                k = int(parts[0])
                out["count"][k] += float(parts[1])
                out["pixels"][k] += float(parts[2])
    return out


def parse_fn(field: str) -> Counter:
    c = Counter()
    for item in field.split(","):
        fn, n = item.rsplit(":", 1)
        c[fn] += int(n)
    return c


def segments(path: str) -> list[dict]:
    """The work segments of one log: from a VSync return (or an instant VSync(-1)/VSync(1),
    or the end of a busy wait) to the next VSync call, while the replay is lined up."""
    out = []
    mode = -1
    start = None
    acc, fn = Counter(), Counter()
    sp = 0
    prims = 0
    drawsync = False
    jobs = []
    last_lined = False
    closed = None  # the segment the last V line closed (its G line follows)
    prev_kind = None
    pre = None
    start_kind = None
    for line in open(path):
        f = line.split()
        if not f or f[0].startswith("#"):
            continue
        k = f[0]
        if k == "G":
            c = parse_fn(f[1])
            if closed is not None:
                closed["fn"].update(c)
            elif prev_kind in ("Q", "D"):
                fn.update(c)
                if prev_kind == "D" and pre is not None:
                    pre["fn"].update(c)
            continue
        prev_kind = k
        closed = None
        if k == "M":
            mode = int(f[1])
            start, acc, fn, sp, drawsync, jobs = None, Counter(), Counter(), 0, False, []
            prims = 0
        elif k in ("Q", "D"):
            t = float(f[2] if k == "Q" else f[1])
            sp += int(f[3] if k == "Q" else f[2])
            pi = 6 if k == "Q" else 4
            prims += int(f[pi]) if len(f) > pi else 0
            acc += parse_gte(f[4] if k == "Q" else f[3])
            if k == "D":
                drawsync = True
                pre = {"fn": Counter(fn), "sp": sp, "prims": prims, "jobs": len(jobs)}
            else:
                jobs.append((f[1], t, parse_gpu(f[5])))
        elif k == "W":
            start = float(f[2])
            start_kind = "W"
            acc, fn, sp, drawsync, jobs = Counter(), Counter(), 0, False, []
            prims = 0
        elif k == "V":
            arg, t = int(f[1]), float(f[2])
            sp += int(f[3])
            prims += int(f[6]) if len(f) > 6 else 0
            acc += parse_gte(f[4])
            lined = len(f) < 6 or f[5] == "1"
            if not lined:
                start = None  # past the log's end (or before its start): the port's own clock
            if start is not None:
                seg = {"log": path, "mode": mode, "t0": start, "t1": t, "end": arg, "gte": acc, "sp": sp,
                       "prims": prims, "drawsync": drawsync, "jobs": jobs, "fn": fn, "pre": pre if drawsync else None,
                       "start_kind": start_kind}
                out.append(seg)
                closed = seg
            acc, fn, sp, drawsync, jobs = Counter(), Counter(), 0, False, []
            prims = 0
            start = t if arg in (-1, 1) and lined else None
            start_kind = f"V{arg}"
            last_lined = lined
        elif k == "R":
            start = float(f[2]) if last_lined else None
            start_kind = "R"
            acc, fn, sp, drawsync, jobs = Counter(), Counter(), 0, False, []
            prims = 0
    for s in out:
        s["dur"] = s["t1"] - s["t0"]
        s["vblanks"] = int(np.floor(s["t1"]) - np.floor(s["t0"]))
    return out


def features(segs: list[dict], modes: list[int], fns: list[str]) -> tuple[np.ndarray, list[str]]:
    """Per-mode constants (modes may be empty), GTE commands by function (the listed ones;
    the rest together), safe points and linked primitives."""
    names = [f"mode{m}" for m in modes] + [f"fn:{f}" for f in fns] + ["fn_other", "safe_points", "prims"]
    col = {f: len(modes) + i for i, f in enumerate(fns)}
    X = np.zeros((len(segs), len(names)))
    for i, s in enumerate(segs):
        if s["mode"] in modes:
            X[i, modes.index(s["mode"])] = 1
        for f, n in s["fn"].items():
            X[i, col.get(f, len(names) - 3)] += n
        X[i, -2] = s["sp"]
        X[i, -1] = s.get("prims", 0)
    return X, names


def nnls(A: np.ndarray, b: np.ndarray, ridge: float = 0.0, iters: int = 20000) -> np.ndarray:
    """min |Ax - b|^2 + ridge |x|^2 over x >= 0 (projected coordinate descent on the
    normal equations; the columns are scaled to unit norm first)."""
    norms = np.linalg.norm(A, axis=0)
    norms[norms == 0] = 1
    An = A / norms
    G = An.T @ An + ridge * np.eye(A.shape[1])
    c = An.T @ b
    x = np.zeros(A.shape[1])
    for _ in range(iters):
        delta = 0.0
        for j in range(len(x)):
            if G[j, j] <= 0:
                continue
            new = max(0.0, x[j] + (c[j] - G[j] @ x) / G[j, j])
            delta = max(delta, abs(new - x[j]))
            x[j] = new
        if delta < 1e-10:
            break
    return x / norms


def cpu_rows(segs: list[dict]) -> list[dict]:
    # Pure CPU work: no DrawSync inside (its wait is the GPU's), some work, and not a
    # stretch the PS1 spent waiting in a way the log does not show (over 6 vblanks).
    return [s for s in segs if not s["drawsync"] and 0 <= s["dur"] < 6 and s["mode"] >= 0]


def report(title: str, segs: list[dict], pred: np.ndarray) -> None:
    dur = np.array([s["dur"] for s in segs])
    err = pred - dur
    print(f"{title}: {len(segs)} segments, PS1 time {dur.sum():.1f} vblanks, mean |error| {np.abs(err).mean():.4f} "
          f"vblanks, RMS {np.sqrt((err ** 2).mean()):.4f}, total {pred.sum() - dur.sum():+.1f}")
    by = defaultdict(list)
    for s, e in zip(segs, err):
        by[s["mode"]].append((s["dur"], e))
    for m in sorted(by):
        d = np.array(by[m])
        big = d[d[:, 0] > 0.25]
        rel = (np.abs(big[:, 1]) / big[:, 0]).mean() if len(big) else float("nan")
        print(f"  mode {m}: {len(d)} segments, {d[:, 0].sum():.1f} vblanks, mean |error| {np.abs(d[:, 1]).mean():.4f}, "
              f"over 0.25 vblank: {len(big)} with mean relative error {rel:.3f}")


def top_functions(rows: list[dict], k: int) -> list[str]:
    tot = Counter()
    for r in rows:
        tot.update(r["fn"])
    return [f for f, n in tot.most_common(k) if n > 0]


def fit_rows(rows: list[dict], k: int, ridge: float, intercepts: bool = False):
    modes = sorted({r["mode"] for r in rows}) if intercepts else []
    fns = top_functions(rows, k)
    X, names = features(rows, modes, fns)
    y = np.array([r["dur"] for r in rows])
    w = nnls(X, y, ridge)
    return {"modes": modes, "fns": fns, "names": names, "w": w}


def predict(model: dict, rows: list[dict]) -> np.ndarray:
    X, _ = features(rows, model["modes"], model["fns"])
    return X @ model["w"]


def header(model: dict, sources: list[str]) -> str:
    cyc = 33868800.0 / 59.826  # vblanks to CPU cycles
    w = dict(zip(model["names"], model["w"] * cyc))
    mode = [0.0] * 8
    for m in model["modes"]:
        if 0 <= m < 8:
            mode[m] = w[f"mode{m}"]
    fns = sorted((f, w[f"fn:{f}"]) for f in model["fns"])
    import os
    names = sorted({os.path.basename(os.path.dirname(p)) or os.path.basename(p) for p in sources})
    lines = ["/* The PS1 time model's costs (xg-wv9t), in CPU cycles (33.8688 MHz). Written by",
             " * tools/ps1time_fit.py from Beetle PSX VSync logs (tools/beetle.py --vsync-log) and the",
             " * port's replays of them (XG_PS1TIME_LOG); do not edit by hand. Fitted on " + str(len(names)) + " runs:",
             " *   " + ", ".join(names), " */"]
    lines.append(f"#define PS1TIME_MODEL_SAFE_POINT {w['safe_points']:.3f} /* per platform call (a safe point) */")
    lines.append(f"#define PS1TIME_MODEL_OTHER {w['fn_other']:.3f} /* per GTE command of a function not listed */")
    lines.append(f"#define PS1TIME_MODEL_PRIM {w['prims']:.3f} /* per primitive linked into an ordering table */")
    lines.append("/* per stretch of work that ends at a VSync call, by game mode (bootGame's 0-7) */")
    lines.append("static const double ps1timeModelMode[8] = {" + ", ".join(f"{v:.1f}" for v in mode) + "};")
    lines.append("/* per GTE command run by the function (include/gte_pc.h's __func__; \"-\": LIBGTE's own),")
    lines.append(" * sorted by name (strcmp) */")
    lines.append("static const struct {\n    const char* fn;\n    double cycles;\n} ps1timeModelFn[] = {")
    for f, v in sorted(fns, key=lambda x: x[0].encode()):
        lines.append(f'    {{"{f}", {v:.3f}}},')
    lines.append("};")
    lines += ["/* the GPU, per queued job: a base, per primitive and per pixel by kind (ps1time.h's",
              " * PT_ order), per semi-transparent and per shaded textured pixel */",
              "#define PS1TIME_MODEL_GPU_JOB 0.0",
              "static const double ps1timeModelGpuCount[13] = {0};",
              "static const double ps1timeModelGpuPixel[13] = {0};",
              "#define PS1TIME_MODEL_GPU_SEMI 0.0",
              "#define PS1TIME_MODEL_GPU_SHADED 0.0", ""]
    return "\n".join(lines)




def frame_check(segs: list[dict], model: dict, gpu_w: np.ndarray | None) -> dict:
    """Per frame (a waiting VSync's return to the next waiting VSync call): whether the
    model ends its work in the same vblank as the PS1 did, so its picture lasts as long."""
    by_mode = defaultdict(lambda: [0, 0, 0, 0])  # frames, same vblank, model later, model earlier
    frame, start = [], None
    gpu_end = None
    for s in segs:
        if start is None or s["t0"] < start:
            start = s["t0"] if s["end"] not in (0,) else None
        frame.append(s)
        if s["end"] == -1 or s["end"] == 1:
            continue
        # a waiting call ends the frame
        t = frame[0]["t0"]
        for f in frame:
            if f["drawsync"] and gpu_w is not None and f["pre"] is not None:
                pre = f["pre"]
                post = Counter(f["fn"])
                post.subtract(pre["fn"])
                ready = t + part(model, pre["fn"], pre["sp"], pre["prims"])
                jobs = sum((gpu_vector(g) for _, _, g in f["jobs"][:pre["jobs"]]), np.zeros(len(GPU_NAMES)))
                t = max(ready, (gpu_end or ready)) + part(model, +post, f["sp"] - pre["sp"], f["prims"] - pre["prims"])
            else:
                t += float(predict(model, [f])[0])
            for _, tq, g in f["jobs"]:
                if gpu_w is not None:
                    gpu_end = max(gpu_end or 0, t) + float(gpu_vector(g) @ gpu_w)
        actual = frame[-1]["t1"]
        if frame[0]["t0"] is not None and actual - frame[0]["t0"] < 6:
            m = by_mode[frame[0]["mode"]]
            m[0] += 1
            a, b = int(np.floor(actual)), int(np.floor(t))
            m[1] += a == b
            m[2] += b > a
            m[3] += b < a
        frame, start = [], None
    return by_mode


GPU_GROUPS = [  # (name, kinds, use pixels)
    ("poly", [0, 1, 2, 3, 4], False), ("rect", [5, 6, 7, 8], False), ("line_px", [9], True),
    ("untex_px", [0, 1, 5], True), ("clut_px", [2, 3, 6, 7], True), ("tex15_px", [4, 8], True),
    ("fill_px", [10], True), ("copy_px", [11], True), ("load_px", [12], True),
]
GPU_NAMES = ["job"] + [g[0] for g in GPU_GROUPS] + ["semi_px", "shaded_px"]


def gpu_vector(job: dict) -> np.ndarray:
    v = [1.0]
    for _, kinds, pixels in GPU_GROUPS:
        v.append(sum(job["pixels" if pixels else "count"][k] for k in kinds))
    v += [job["semi"], job["shaded"]]
    return np.array(v)


def part(model: dict, fn: Counter, sp: float, prims: float) -> float:
    seg = {"mode": -1, "fn": fn, "sp": sp, "prims": prims}
    return float(predict(model, [seg])[0])


def gpu_observations(segs: list[dict], model: dict) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Per DrawSync(0) that ends a frame's CPU work (field, battle: right after its
    VSync(1)): the GPU work queued since the frame began (a waiting VSync returned), and
    the time the GPU must have taken from the first of it (bound=1: DrawSync waited) or
    at most took (bound=0)."""
    X, y, bound = [], [], []
    queued, first = None, None
    for s in segs:
        if s["start_kind"] == "R":
            queued, first = None, None
        jobs = s["jobs"]
        cut = s["pre"]["jobs"] if s["drawsync"] else len(jobs)
        for name, t, g in jobs[:cut]:
            if queued is None:
                queued, first = np.zeros(len(GPU_NAMES)), t
            queued += gpu_vector(g)
        if s["drawsync"] and queued is not None and s["start_kind"] == "V1" and s["dur"] < 1.5 \
                and s["mode"] in (1, 2):
            pre = s["pre"]
            post_fn = Counter(s["fn"])
            post_fn.subtract(pre["fn"])
            cpu_post = part(model, +post_fn, s["sp"] - pre["sp"], s["prims"] - pre["prims"])
            done = s["t1"] - cpu_post
            X.append(queued.copy())
            if done - s["t0"] > 0.02:
                y.append(done - first)
                bound.append(1)
            else:
                y.append(s["t0"] - first)
                bound.append(0)
        if s["drawsync"]:
            queued, first = None, None
            for name, t, g in jobs[cut:]:
                if queued is None:
                    queued, first = np.zeros(len(GPU_NAMES)), t
                queued += gpu_vector(g)
    return np.array(X), np.array(y), np.array(bound)


def fit_gpu(X: np.ndarray, y: np.ndarray, bound: np.ndarray, rounds: int = 8) -> np.ndarray:
    """GPU costs >= 0: least squares on the stretches the GPU held up, and the other
    stretches' times as upper limits (an exceeded limit joins the fit at its limit)."""
    use = bound == 1
    w = np.zeros(X.shape[1])
    for _ in range(rounds):
        if use.sum() == 0:
            break
        w = nnls(X[use], y[use], 1e-6)
        over = (bound == 0) & (X @ w > y + 0.01)
        if not over.any() or (use | over).sum() == use.sum():
            break
        use = use | over
    return w


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("segments")
    a.add_argument("logs", nargs="+")
    a = sub.add_parser("gpu")
    a.add_argument("logs", nargs="+")
    a.add_argument("--functions", type=int, default=60)
    a.add_argument("--ridge", type=float, default=1e-6)
    for name in ("fit", "cv"):
        a = sub.add_parser(name)
        a.add_argument("logs", nargs="+")
        a.add_argument("--functions", type=int, default=20, help="functions with a cost of their own")
        a.add_argument("--ridge", type=float, default=1e-6)
        a.add_argument("--intercepts", action="store_true", help="a constant per stretch, by game mode")
        if name == "fit":
            a.add_argument("--test", nargs="*", default=[])
            a.add_argument("--header", help="write port/src/system/ps1time_model.h here")
            a.add_argument("--model-file", help="write the costs for XG_PS1TIME_MODEL (validation runs)")
    args = ap.parse_args()

    if args.cmd == "segments":
        segs = [s for p in args.logs for s in segments(p)]
        by = defaultdict(list)
        for s in segs:
            by[(s["mode"], s["drawsync"])].append(s)
        for (m, d), ss in sorted(by.items()):
            dur = np.array([s["dur"] for s in ss])
            print(f"mode {m} {'with DrawSync' if d else 'CPU only   '}: {len(ss):6d} segments, {dur.sum():9.1f} vblanks, "
                  f"median {np.median(dur):.4f}, max {dur.max():.3f}")
        return 0

    if args.cmd == "gpu":
        allsegs = {p: segments(p) for p in args.logs}
        model = fit_rows([r for p in args.logs for r in cpu_rows(allsegs[p])], args.functions, args.ridge)
        obs = [gpu_observations(allsegs[p], model) for p in args.logs]
        X = np.vstack([o[0] for o in obs if len(o[0])])
        y = np.concatenate([o[1] for o in obs if len(o[0])])
        b = np.concatenate([o[2] for o in obs if len(o[0])])
        w = fit_gpu(X, y, b)
        p = X @ w
        cyc = 33868800.0 / 59.826
        print(f"{len(y)} DrawSync stretches, {int(b.sum())} held up by the GPU")
        for name, v in zip(GPU_NAMES, w):
            print(f"  {name:10s} {v * cyc:10.4f} cycles")
        e = (p - y)[b == 1]
        print(f"held up: mean |error| {np.abs(e).mean():.4f} vblanks; limits exceeded by > 0.02: "
              f"{int(((p - y) > 0.02)[b == 0].sum())} of {int((b == 0).sum())}")
        return 0
    data = {p: cpu_rows(segments(p)) for p in args.logs}
    if args.cmd == "cv":
        for held in args.logs:
            rows = [r for p in args.logs if p != held for r in data[p]]
            model = fit_rows(rows, args.functions, args.ridge, args.intercepts)
            report(f"held out {held}", data[held], predict(model, data[held]))
            fc = frame_check(segments(held), model, None)
            for m in sorted(fc):
                n, same, late, early = fc[m]
                if n:
                    print(f"  frames mode {m}: {n}, work ends in the PS1's vblank {same / n:.1%}, later {late / n:.1%}, "
                          f"earlier {early / n:.1%}")
        return 0
    rows = [r for p in args.logs for r in data[p]]
    model = fit_rows(rows, args.functions, args.ridge, args.intercepts)
    report("fit", rows, predict(model, rows))
    cyc = 33868800.0 / 59.826
    for n, v in sorted(zip(model["names"], model["w"]), key=lambda x: -x[1])[:30]:
        print(f"  {n:40s} {v * cyc:10.1f} cycles")
    for p in args.test:
        t = cpu_rows(segments(p))
        report(f"test {p}", t, predict(model, t))
    if args.header:
        open(args.header, "w").write(header(model, args.logs))
    if args.model_file:
        w = dict(zip(model["names"], model["w"] * cyc))
        out = [f"safe_point {w['safe_points']:.4f}", f"prim {w['prims']:.4f}", f"other {w['fn_other']:.4f}"]
        out += [f"mode {m} {w[f'mode{m}']:.4f}" for m in model["modes"] if 0 <= m < 8]
        out += [f"fn {f} {w[f'fn:{f}']:.4f}" for f in model["fns"]]
        open(args.model_file, "w").write("\n".join(out) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
