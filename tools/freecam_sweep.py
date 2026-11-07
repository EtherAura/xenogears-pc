#!/usr/bin/env python3
"""Per-room limits for the free field camera (port/src/system/freecam.c), found by sweeping.

Each room runs headless under the Enhanced engine with XG_FREECAM_SWEEP: from a few positions
(the party walks up, right, down, left between cycles) the camera steps through every world
heading, pitch and zoom of the engine's grid, holding each pose with the characters drawn and
then hidden, while the room's clear colour is magenta. A pose is scored by

  void      the share of the frame left magenta (open space past the modelled room) beyond
            what the game's own view of that position shows,
  visible   the share of the party's pixels near the screen centre (the tracked actor; the
            frames' difference with and without characters) against the clearest view of
            that cycle and zoom: a wall between camera and party drops it (kept for phase 2b;
            not a limit).

A room's limit for a heading and zoom is the band of pitches that shows no void from most
sampled positions (a high camera can look over an interior's walls into the void as a low one
can past its floor), or forbidden when that band is too narrow to tilt in; `table` writes them as
port/src/system/freecam_limits.h, which the engine compiles in and applies with XG_FREECAM_LIMITS=1
(off by default: the user prefers the whole range, dark past the room and all).

    python tools/freecam_sweep.py run OUT [ROOM...]      # ROOM: field[:progress[:party]]; scores each
                                                          # room and drops its frames (--keep keeps them)
    python tools/freecam_sweep.py score OUT
    python tools/freecam_sweep.py table OUT [--emit port/src/system/freecam_limits.h]

Default rooms: Lahan (fields 1-14) and the rooms up to the end of the Blackmoon Forest chapter
(work/art/field-tex/story-points.txt, the first story point of each field).
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / 'build/pc/enhanced/xenogears-enhanced'
DISC = ROOT / 'bin/Xenogears (USA) (Disc 1)/Xenogears (USA) (Disc 1).cue'
STORY = ROOT / 'work/art/field-tex/story-points.txt'

# the engine's grid (freecam.c sweepPitch / sweepZoom / SWEEP_HEADINGS / SWEEP_HOLD)
HEADINGS = 16
PITCHES = [8, 15, 25, 35, 50, 65]
ZOOMS = [0.6, 1.0, 1.4]
HOLD = 6
LENGTH = HEADINGS * len(PITCHES) * len(ZOOMS) * HOLD
FIRST, GAP, CYCLES, WALK = 600, 84, 5, 60
WALKS = ['1000', '2000', '4000', '8000']   # up, right, down, left between cycles

# A pose shows void when its magenta share exceeds the game's own view's (same position and
# zoom; the interiors sit in a dark surround that is part of the original picture) by more
# than VOID_EXTRA of the frame; it is refused when it shows void from at least VOID_SHARE of
# the sampled positions. What hides the party (a house between camera and party) is left to
# phase 2b (the camera pulls in, what is in front fades); `visible` is kept in the scores.
VOID_EXTRA = 0.05
VOID_SHARE = 0.2
VISIBLE_MIN = 0.4       # of the cycle's clearest view (reported, not a limit)
PITCH_LOW, PITCH_HIGH = 8, 75   # the camera's own pitch range (freecam.c PITCH_MIN / PITCH_MAX)
BAND_MIN = 15                   # narrower usable bands are refused: the pitch is never pinned
FORBIDDEN = 255

LOG = re.compile(r'freecam sweep: vb (\d+) field (-?\d+) posed (\d) heading ([\d.]+) pitch (\d+) zoom ([\d.]+) '
                 r'chars (\d) free (\d) at (-?\d+) (-?\d+) (-?\d+) game (-?[\d.]+) (-?[\d.]+)')


def rooms(args):
    if args:
        out = []
        for a in args:
            bits = a.split(':')
            out.append(dict(field=int(bits[0]), progress=bits[1] if len(bits) > 1 else None,
                            party=bits[2] if len(bits) > 2 else '0,255,255'))
        return out
    out = [dict(field=f, progress=None, party='0,255,255') for f in range(1, 15)]
    seen = {r['field'] for r in out}
    for line in STORY.read_text().splitlines():
        bits = line.split()
        if not bits or line.startswith('#') or not bits[1].isdigit():
            continue
        f = int(bits[1])
        if f not in seen:
            seen.add(f); out.append(dict(field=f, progress=bits[2], party=bits[3]))
    return out


def pad_script(path):
    lines = ['0 0000']
    period = LENGTH + GAP
    for c in range(CYCLES - 1):
        start = FIRST + c * period + LENGTH
        lines += [f'{start + 6} {WALKS[c % len(WALKS)]}', f'{start + 6 + WALK} 0000']
    path.write_text('\n'.join(lines) + '\n')


def run_room(out, room, keep=False):
    d = out / f"f{room['field']:02d}"
    d.mkdir(parents=True, exist_ok=True)
    pad_script(d / 'sweep.pad')
    last = FIRST + CYCLES * (LENGTH + GAP)
    env = dict(HOME=os.environ['HOME'], PATH=os.environ['PATH'], XDG_RUNTIME_DIR=os.environ.get('XDG_RUNTIME_DIR', ''),
               XG_HEADLESS='1', SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy', XG_SETTINGS='none',
               XG_BIOS='none', XG_CLOCK='step', XG_CD_SPEED='8', XG_CARD_DIR=str(d / 'cards'), XG_REWIND='0',
               XG_VK_DEVICE='Intel', XG_UPSCALE='1', XG_FIELD=str(room['field']), XG_PARTY=room['party'],
               XG_QUIT_AT=str(last + 10), XG_PAD_SCRIPT=str(d / 'sweep.pad'),
               XG_FREECAM_SWEEP=f'{FIRST},{GAP},{CYCLES}', XG_DUMP_FRAMES=str(d / 'frames'),
               XG_DUMP_RANGE=f'{FIRST}-{last}')
    if room['progress'] is not None:
        env['XG_GAMESTATE'] = f"0x1930={room['progress']}"
    with open(d / 'run.log', 'w') as log:
        rc = subprocess.run([str(EXE), str(DISC)], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=1800).returncode
    (d / 'room.json').write_text(json.dumps(room) + '\n')
    n = len(list((d / 'frames').glob('*.png'))) if (d / 'frames').exists() else 0
    scored = len(score_room(d)['cells']) if n else 0
    if not keep and (d / 'frames').exists():   # about 1 GB a room: the scores are what is kept
        shutil.rmtree(d / 'frames')
    return f"field {room['field']}: exit {rc}, {n} frames, {scored} poses scored"


def cmd_run(a):
    out = Path(a.out)
    with ThreadPoolExecutor(a.jobs) as ex:
        for msg in ex.map(lambda r: run_room(out, r, a.keep), rooms(a.rooms)):
            print(msg, flush=True)


def load(path):
    return np.asarray(Image.open(path).convert('RGB')).astype(np.int16)


def calibrate(frames):
    """The vblanks between the engine setting a pose and the frame showing it: the camera and
    the character toggle reach the picture with different delays (the camera goes through the
    field's matrix double buffer and the Enhanced frame pacing). Measured on the first cycle:
    a camera change moves the picture outside the centre; a character toggle only inside."""
    cam, chars = {}, {}
    prev = pv = None
    for vb in range(FIRST, FIRST + 600):
        if vb not in frames:
            continue
        a = load(frames[vb])
        if prev is not None and vb == pv + 1:
            diff = np.abs(a - prev).sum(-1) > 40
            H, W = diff.shape
            centre = (slice(H // 4, H * 3 // 4), slice(W // 3, W * 2 // 3))
            outside = diff.copy(); outside[centre] = False
            if outside.mean() > 0.2:
                cam[(vb - FIRST) % HOLD] = cam.get((vb - FIRST) % HOLD, 0) + 1
            elif diff[centre].mean() > 0.02:
                k = (vb - FIRST) % (HOLD // 2)
                chars[k] = chars.get(k, 0) + 1
        prev, pv = a, vb
    lc = max(cam, key=cam.get) if cam else 0
    lh = max(chars, key=chars.get) if chars else 0
    return lc, lh


def score_room(d):
    poses = {}
    for line in (d / 'run.log').read_text(errors='replace').splitlines():
        m = LOG.search(line)
        if m:
            poses[int(m[1])] = m
    room = json.loads((d / 'room.json').read_text())
    frames = {int(p.stem.split('_')[1]): p for p in (d / 'frames').glob('frame_*.png')}
    period = LENGTH + GAP
    lc, lh = calibrate(frames)
    cells = []
    for c in range(CYCLES):
        base = FIRST + c * period
        for cell in range(LENGTH // HOLD):
            vb0 = base + cell * HOLD
            # the frames showing this pose: vb0 + lc .. vb0 + lc + HOLD - 1 (the first one may still
            # be blending); in each, the characters are drawn when the toggle (delay lh) was in the
            # first half of its cycle. The latest frame of each kind is taken.
            window = [vb for vb in range(vb0 + lc + 1, vb0 + lc + HOLD) if vb in frames]
            drawn = lambda vb: (vb - lh - FIRST) % HOLD >= HOLD // 2   # measured: drawn in the later half
            on = next((vb for vb in reversed(window) if drawn(vb)), None)
            off = next((vb for vb in reversed(window) if not drawn(vb)), None)
            m = poses.get(vb0)
            if on is None or off is None or m is None or int(m[3]) != 1:
                continue
            a, b = load(frames[on]), load(frames[off])
            H, W = a.shape[:2]
            magenta = (a[..., 0] > 200) & (a[..., 1] < 70) & (a[..., 2] > 200)
            zoom = float(m[6])
            hw, up, dn = int(60 / zoom), int(110 / zoom), int(40 / zoom)
            cx, cy = W // 2, H // 2
            win = (slice(max(0, cy - up), min(H, cy + dn)), slice(max(0, cx - hw), min(W, cx + hw)))
            party = (np.abs(a[win] - b[win]).sum(-1) > 60).sum()
            cells.append(dict(cycle=c, heading=float(m[4]), pitch=int(m[5]), zoom=zoom, field=int(m[2]),
                              free=int(m[8]), void=float(magenta.mean()), party=int(party),
                              at=[int(m[9]), int(m[10]), int(m[11])], game=[float(m[12]), float(m[13])]))
    # visibility against each cycle's clearest view at that zoom
    for c in range(CYCLES):
        for z in ZOOMS:
            group = [x for x in cells if x['cycle'] == c and x['zoom'] == z]
            ref = np.percentile([x['party'] for x in group], 90) if group else 0
            for x in group:
                x['visible'] = x['party'] / ref if ref else 0.0
    out = dict(room=room, lag=dict(camera=lc, characters=lh), cells=cells)
    (d / 'score.json').write_text(json.dumps(out) + '\n')
    return out


def cmd_score(a):
    for d in sorted(Path(a.out).glob('f[0-9]*')):
        if (d / 'run.log').exists():
            s = score_room(d)
            print(f"{d.name}: {len(s['cells'])} poses scored", flush=True)


def band(ok):
    """The usable pitch band (low, high) in degrees from the grid pitches that pass: the widest
    run of passing pitches, its edges halfway to the failing neighbours (the grid's ends reach
    the camera's 8..75); FORBIDDEN when none passes or the band is narrower than BAND_MIN, so the
    camera stops turning there instead of pinning the pitch."""
    runs, cur = [], []
    for p in PITCHES:
        if ok[p]:
            cur.append(p)
        elif cur:
            runs.append(cur); cur = []
    if cur:
        runs.append(cur)
    if not runs:
        return FORBIDDEN
    best = max(runs, key=lambda r: (r[-1] - r[0], -r[0]))
    i, j = PITCHES.index(best[0]), PITCHES.index(best[-1])
    lo = PITCH_LOW if i == 0 else (PITCHES[i - 1] + best[0]) / 2
    hi = PITCH_HIGH if j == len(PITCHES) - 1 else (best[-1] + PITCHES[j + 1]) / 2
    return FORBIDDEN if hi - lo < BAND_MIN else (round(lo), round(hi))


def limits(score):
    """[zoom][heading] -> (low, high) pitch band in degrees or FORBIDDEN, from a room's scored
    poses; None for a room the free camera never runs in (the game's camera is fixed there)."""
    field = score['room']['field']
    cells = [x for x in score['cells'] if x['field'] == field]
    if not cells or not any(x['free'] for x in cells):
        return None
    cycles = sorted({x['cycle'] for x in cells if x['free']})
    base = {}   # (cycle, zoom): the void of the pose nearest the game's own view
    for c in cycles:
        for z in ZOOMS:
            group = [x for x in cells if x['cycle'] == c and x['zoom'] == z]
            if not group:
                continue
            gh, gp = group[0]['game'][0] % 360, group[0]['game'][1]
            near = min(group, key=lambda x: (min(abs(x['heading'] - gh), 360 - abs(x['heading'] - gh)), abs(x['pitch'] - gp)))
            base[(c, z)] = near['void']
    table = []
    for z in ZOOMS:
        row = []
        for h in range(HEADINGS):
            heading = h * 360 / HEADINGS
            ok = {}
            for p in PITCHES:
                group = [x for x in cells if x['cycle'] in cycles and x['zoom'] == z and abs(x['heading'] - heading) < 0.1
                         and x['pitch'] == p]
                # a pose no position sampled (an event, a room change) sets no limit
                ok[p] = not group or \
                    sum(x['void'] > base.get((x['cycle'], z), 0) + VOID_EXTRA for x in group) < VOID_SHARE * len(group)
            row.append(band(ok))
        table.append(row)
    return table


def show(v):
    return '  --  ' if v == FORBIDDEN else f'{v[0]:2d}-{v[1]:2d} '


def cmd_table(a):
    rows = []
    for d in sorted(Path(a.out).glob('f[0-9]*')):
        if not (d / 'score.json').exists():
            continue
        score = json.loads((d / 'score.json').read_text())
        t = limits(score)
        field = score['room']['field']
        if t is None:
            print(f'field {field:3d}: fixed camera (free camera never runs)')
            continue
        rows.append((field, t))
        print(f'field {field:3d}:')
        for z, r in zip(ZOOMS, t):
            print(f'  zoom {z}: ' + ''.join(show(v) for v in r))
    if a.emit:
        cell = lambda v: '{255, 255}' if v == FORBIDDEN else f'{{{v[0]}, {v[1]}}}'
        lines = ['/* Generated by tools/freecam_sweep.py table: per-room limits of the free field camera.',
                 ' * For each room, zoom (of the game\'s distance) and world heading of the eye from the target',
                 ' * (16 steps from +x toward +z), the pitch band in degrees (low, high) that shows no more open',
                 ' * void than the game\'s own view from most sampled positions; {255, 255}: the camera does not',
                 ' * turn there. Rooms not listed have no limits (or a fixed camera). The engine applies them only',
                 ' * with XG_FREECAM_LIMITS=1 (A/B): by default the camera has its whole range everywhere. */',
                 f'#define FREECAM_LIMIT_HEADINGS {HEADINGS}', f'#define FREECAM_LIMIT_ZOOMS {len(ZOOMS)}',
                 '#define FREECAM_LIMIT_FORBIDDEN 255',
                 'static const double freecamLimitZoom[FREECAM_LIMIT_ZOOMS] = {' + ', '.join(str(z) for z in ZOOMS) + '};',
                 'static const struct { short field; unsigned char pitch[FREECAM_LIMIT_ZOOMS][FREECAM_LIMIT_HEADINGS][2]; } freecamLimits[] = {']
        for field, t in rows:
            lines.append(f'    {{{field}, {{' + ', '.join('{' + ', '.join(cell(v) for v in r) + '}' for r in t) + '}},')
        lines.append('};')
        Path(a.emit).write_text('\n'.join(lines) + '\n')
        print(f'{a.emit}: {len(rows)} rooms')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('run'); r.add_argument('out'); r.add_argument('rooms', nargs='*'); r.add_argument('--jobs', type=int, default=4); r.add_argument('--keep', action='store_true', help='keep the frames')
    s = sub.add_parser('score'); s.add_argument('out')
    t = sub.add_parser('table'); t.add_argument('out'); t.add_argument('--emit')
    a = ap.parse_args()
    dict(run=cmd_run, score=cmd_score, table=cmd_table)[a.cmd](a)


if __name__ == '__main__':
    main()
