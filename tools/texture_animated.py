#!/usr/bin/env python3
"""Field texture pages with an animated region (xg-3hyf.4): one redraw for every frame of a page of which a small
part animates (blinking console lights, a scrolling strip). Each frame of such a page is its own texel key, so a
redraw keyed by one frame matched that frame only and those pages were left original (pages-left.txt). The port
now names them by a still key (docs/TEXTURE_PACKS.md, animated.txt): the texel key with every texel of the
animated region read as 0. A pack lists each such page in animated.txt beside rooms.txt,

    <still key> <w> <h> <depth> <x>,<y>,<w>,<h> ...      (the region's rectangles, in the page image's texels)

and draws the page's image outside the region and the game's own texels inside it, so the animation goes on.

Here: the frames of a page found in the discovery dumps (pages of one size and depth sharing a room whose texels
differ in under --limit of the page, single linkage), the region (every texel that differs between them, covered
by aligned BLOCK x BLOCK squares, merged into at most RECTS rectangles) and the still key, which every frame of the
cluster must give (else the cluster is refused). work/art/field-tex/atlas.py --animated merges each cluster into one
page (its samples and palettes the union of the frames', the region's texels out of its tiles), so the plan paints it
once; field_texture_redraw.py build writes animated.txt.

    find ATLAS.json [--limit F] [--keys K,...|@FILE ...] [--min-static N]
        the clusters among the atlas's pages (or only those keys: pages-left.txt files with @), each with its
        region, still key and how many of its sampled texels lie outside the region (what a redraw would change)
    selftest
"""
import argparse, json, sys
from pathlib import Path

import numpy as np

BLOCK = 8      # the region's squares, in texels
RECTS = 64     # rectangles per page at most (the port's ANIM_RECTS)
FNV0, FNVP, M64 = 14695981039346656037, 1099511628211, (1 << 64) - 1


def _fnv(h, data):
    for b in data: h = ((h ^ b) * FNVP) & M64
    return h


def texel_key(depth, w, h, texels):
    """texture.c's texel key: FNV-1a64 of 'xgtex1', the depth byte, LE u16 width and height, the row-major index
    bytes (4/8-bit) or LE words (15-bit), as a 16-digit hex string."""
    head = b'xgtex1' + bytes([depth, w & 255, w >> 8, h & 255, h >> 8])
    t = np.asarray(texels)
    body = t.astype('<u2').tobytes() if depth == 15 else t.astype(np.uint8).tobytes()
    return f'{_fnv(_fnv(FNV0, head), body):016x}'


def region_mask(w, h, rects):
    m = np.zeros((h, w), bool)
    for x, y, rw, rh in rects: m[y:y + rh, x:x + rw] = True
    return m


def still_key(depth, w, h, texels, rects):
    """The page's key with its region's texels read as 0 (texture_pack.c animStillKey)."""
    t = np.array(texels).reshape(h, w); t[region_mask(w, h, rects)] = 0
    return texel_key(depth, w, h, t)


def rects_of(mask, block=BLOCK):
    """Aligned block x block squares covering mask's True texels, as rectangles (runs of squares a block row,
    merged downward while the run is the same), clipped to the page; a coarser block until at most RECTS."""
    h, w = mask.shape
    while True:
        bh, bw = -(-h // block), -(-w // block)
        cover = np.zeros((bh, bw), bool)
        ys, xs = np.nonzero(mask); cover[ys // block, xs // block] = True
        rects, active = [], {}   # active: (x0, x1) -> [x0, y0, w, h] reaching the row above
        for by in range(bh):
            runs, bx = [], 0
            while bx < bw:
                if not cover[by, bx]: bx += 1; continue
                x0 = bx
                while bx < bw and cover[by, bx]: bx += 1
                runs.append((x0, bx))
            now = {}
            for x0, x1 in runs:
                r = active.pop((x0, x1), None)
                if r: r[3] += 1
                else: r = [x0, by, x1 - x0, 1]
                now[(x0, x1)] = r
            rects += active.values(); active = now
        rects += active.values()
        out = []
        for x0, y0, rw, rh in sorted(rects, key=lambda r: (r[1], r[0])):
            x, y = x0 * block, y0 * block
            out.append([x, y, min(rw * block, w - x), min(rh * block, h - y)])
        if len(out) <= RECTS: return out
        block *= 2


def clusters(pages, limit):
    """pages: key -> dict(depth, w, h, rooms, texels (h x w array)). Groups of keys (2 or more) of one size and depth
    linked by a room in common and under `limit` of their texels differing."""
    keys = sorted(pages); parent = {k: k for k in keys}
    def root(k):
        while parent[k] != k: parent[k] = parent[parent[k]]; k = parent[k]
        return k
    by = {}
    for k in keys:
        p = pages[k]
        if p['depth'] == 15: continue
        by.setdefault((p['depth'], p['w'], p['h']), []).append(k)
    for group in by.values():
        for i, a in enumerate(group):
            A = pages[a]
            for b in group[i + 1:]:
                B = pages[b]
                if not set(A['rooms']) & set(B['rooms']) or root(a) == root(b): continue
                if (A['texels'] != B['texels']).mean() < limit: parent[root(b)] = root(a)
    out = {}
    for k in keys: out.setdefault(root(k), []).append(k)
    return [sorted(v) for v in out.values() if len(v) > 1]


def describe(pages, frames):
    """A cluster's region (rects), still key and reference frame (the one whose region holds the most non-zero
    texels: the planned canvases show it as context), or None when the frames do not share one still key."""
    p0 = pages[frames[0]]; w, h, depth = p0['w'], p0['h'], p0['depth']
    stack = np.stack([pages[k]['texels'] for k in frames])
    diff = (stack != stack[0]).any(0)
    rects = rects_of(diff)
    keys = {still_key(depth, w, h, pages[k]['texels'], rects) for k in frames}
    if len(keys) != 1: return None
    m = region_mask(w, h, rects)
    ref = max(frames, key=lambda k: int((pages[k]['texels'][m] != 0).sum()))
    return dict(still=keys.pop(), rects=rects, frames=frames, reference=ref, region=int(m.sum()))


def merge_atlas_pages(pages, samples, rooms, texels_of, limit):
    """atlas.py --animated: each cluster of `pages` (atlas.py's load(): key -> page, samples[key][palette] rects,
    rooms[key]) becomes one page under its still key: the reference frame's page, every frame's palettes, samples and
    rooms, and 'animated' = dict(rects, frames). texels_of(page) -> the page's indices (h x w). Returns the merged
    clusters (describe()'s dicts)."""
    info = {k: dict(depth=p['depth'], w=p['w'], h=p['h'], rooms=sorted(rooms[k]), texels=texels_of(p))
            for k, p in pages.items() if k in samples and p['depth'] != 15}
    done = []
    for frames in clusters(info, limit):
        c = describe(info, frames)
        if c is None:
            print(f'animated: {len(frames)} pages ({frames[0]}, ...) differ, but not in one region: left as they are', file=sys.stderr)
            continue
        s = c['still']
        if s in pages and s not in frames:
            # a page whose key is the still key has the frames' texels with the region all 0 (a frame seen while the
            # region was outside the page's load): the same page, one more frame (too different to be linked)
            frames = sorted(frames + [s]); c['frames'] = frames
        page = dict(pages[c['reference']]); page['palettes'] = {}
        merged_samples, merged_rooms = {}, set()
        for k in frames:
            page['palettes'].update(pages[k]['palettes'])
            for pal, rs in samples.get(k, {}).items(): merged_samples.setdefault(pal, []).extend(rs)
            merged_rooms |= set(rooms[k])
        page['animated'] = dict(rects=c['rects'], frames=frames)
        for k in frames:
            pages.pop(k, None); samples.pop(k, None); rooms.pop(k, None)
        pages[s] = page; samples[s] = merged_samples; rooms[s] = merged_rooms
        done.append(c)
    return done


def atlas_pages(atlas):
    """atlas.json entries as clusters() reads them."""
    from PIL import Image
    out = {}
    for k, e in atlas.items():
        if e['depth'] == 15: continue
        out[k] = dict(depth=e['depth'], w=e['w'], h=e['h'], rooms=e['rooms'],
                      texels=np.asarray(Image.open(e['indices']).convert('L')).astype(np.uint8))
    return out


def sampled_mask(entry):
    m = np.zeros((entry['h'], entry['w']), bool)
    for v in entry['palettes'].values():
        for x, y, w, h, n in v['tiles']: m[y:y + h, x:x + w] = True
    return m


def cmd_find(a):
    atlas = json.loads(Path(a.atlas).read_text())
    keys = set()
    for spec in a.keys:
        if spec.startswith('@'):
            keys |= {l.split()[0] for l in open(spec[1:]) if l.strip() and not l.startswith('#')}
        else: keys |= set(spec.split(','))
    sub = {k: e for k, e in atlas.items() if not keys or k in keys}
    pages = atlas_pages(sub)
    found = []
    for frames in clusters(pages, a.limit):
        c = describe(pages, frames)
        if c is None: print(f'{len(frames)} pages from {frames[0]}: no single region'); continue
        e0 = atlas[frames[0]]; m = region_mask(e0['w'], e0['h'], c['rects'])
        static = sum(int((sampled_mask(atlas[k]) & ~m).sum()) for k in frames[:1])
        rooms = sorted({r for k in frames for r in atlas[k]['rooms']})
        c.update(rooms=rooms, w=e0['w'], h=e0['h'], depth=e0['depth'], static_sampled=static)
        found.append(c)
        ok = 'plan' if static >= a.min_static else 'too little static'
        print(f"{c['still']} {e0['w']}x{e0['h']} {e0['depth']}-bit rooms {rooms}: {len(frames)} frames, region {c['region']} texels in "
              f"{len(c['rects'])} rects, {static} sampled texels outside it: {ok}")
    if keys:
        clustered = {k for c in found for k in c['frames']}
        print(f'{len(clustered)} of {len(keys & set(atlas))} listed pages in {len(found)} clusters; not clustered: {len((keys & set(atlas)) - clustered)}')
    if a.json: Path(a.json).write_text(json.dumps(found, indent=1) + '\n')


def selftest():
    rng = np.random.default_rng(1)
    base = rng.integers(1, 256, (256, 256), dtype=np.uint8)
    frames = {}
    for i in range(4):
        t = base.copy(); t[224:, 16:88] = rng.integers(0, 256, (32, 72)); frames[f'{i:016x}'] = dict(depth=8, w=256, h=256, rooms=[1], texels=t)
    frames['ffff000000000000'] = dict(depth=8, w=256, h=256, rooms=[2], texels=base.copy())   # another room: not linked
    cl = clusters(frames, 0.05)
    assert cl == [[f'{i:016x}' for i in range(4)]], cl
    c = describe(frames, cl[0])
    assert c['rects'] == [[16, 224, 72, 32]], c['rects']
    m = region_mask(256, 256, c['rects']); t = base.copy(); t[m] = 0
    assert c['still'] == texel_key(8, 256, 256, t)
    # a known key: texture.c's FNV over 'xgtex1' depth w h and zero texels
    z = np.zeros((2, 3), np.uint8)
    assert texel_key(8, 3, 2, z) == f'{_fnv(_fnv(FNV0, b"xgtex1" + bytes([8, 3, 0, 2, 0])), bytes(6)):016x}'
    many = np.zeros((256, 256), bool); many[::16, ::16] = True
    assert len(rects_of(many)) <= RECTS
    print('texture_animated selftest: ok')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('find'); s.add_argument('atlas'); s.add_argument('--limit', type=float, default=0.05)
    s.add_argument('--keys', action='append', default=[]); s.add_argument('--min-static', type=int, default=64); s.add_argument('--json')
    sub.add_parser('selftest')
    a = ap.parse_args()
    if a.cmd == 'find': cmd_find(a)
    else: selftest()


if __name__ == '__main__':
    main()
