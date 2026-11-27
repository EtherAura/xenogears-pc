"""field_texture_redraw plan() and the ingest colour gate. plan(): a tile with the texels and shape of one already repainted (in this
plan or an earlier --reuse plan) is an alias of that tile; a same-shaped tile with other texels is
planned itself (xg-7oq). Synthetic pages and CLUTs only, written to a temporary directory."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

import numpy as np
from PIL import Image

_spec = importlib.util.spec_from_file_location(
    'field_texture_redraw', Path(__file__).with_name('field_texture_redraw.py'))
ftr = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ftr)

SIDE = 32     # a page's side in texels
TILE = 16     # every tile here is 16x16: one piece each
PAL = '0'


def texels(seed):
    """A 16x16 block of 4-bit indices (1..15), different for each seed."""
    return np.random.default_rng(seed).integers(1, 16, (TILE, TILE))


class Fixture:
    def __init__(self, root):
        self.root = Path(root)
        clut = self.root / 'clut.bin'
        # 16 distinct opaque 15-bit colours (word 0 would be transparent)
        clut.write_bytes(np.array([(i + 1) | (i + 1) << 5 | (15 - i) << 10 for i in range(16)], '<u2').tobytes())
        self.clut = str(clut)

    def page(self, key, blocks):
        """A page whose blocks ({(x, y): seed}) hold texels(seed); 0 elsewhere."""
        idx = np.zeros((SIDE, SIDE), np.uint8)
        for (x, y), seed in blocks.items():
            idx[y:y + TILE, x:x + TILE] = texels(seed)
        path = self.root / f'{key}.png'
        Image.fromarray(idx, 'L').save(path)
        tiles = [[x, y, TILE, TILE, TILE * TILE] for x, y in blocks]
        return key, dict(indices=str(path), w=SIDE, h=SIDE, depth=4, rooms=[1],
                         palettes={PAL: dict(clut=self.clut, tiles=tiles)})

    def plan(self, name, pages, reuse=()):
        atlas = self.root / f'{name}.atlas.json'
        atlas.write_text(json.dumps(dict(pages)))
        d = self.root / name
        a = SimpleNamespace(atlas=str(atlas), dir=str(d), rooms=None, pages=None, min_texels=64,
                            reuse=[str(r) for r in reuse], covered=None)
        with contextlib.redirect_stdout(io.StringIO()):
            ftr.plan(a)
        return d, json.loads((d / 'plan.json').read_text())


def planned(st):
    return sorted((p['page'], p['palette'], p['tile']) for p in st['pieces'])


class PlanReuseTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.fx = Fixture(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_same_texels_on_two_pages_plan_one_tile_and_an_alias_of_it(self):
        # page a: the repeated tile at (0, 0) and a same-shaped other tile at (16, 16);
        # page b: the repeated tile at another rect, (16, 0), so only its texels can pair it
        _, st = self.fx.plan('p', [self.fx.page('a', {(0, 0): 1, (16, 16): 2}),
                                   self.fx.page('b', {(16, 0): 1})])
        self.assertEqual(planned(st), [('a', PAL, [0, 0, TILE, TILE]), ('a', PAL, [16, 16, TILE, TILE])])
        self.assertEqual(len(st['pieces']), 2)
        self.assertEqual(st['aliases'], [dict(page='b', palette=PAL, tile=[16, 0, TILE, TILE],
                                              of=dict(page='a', palette=PAL, tile=[0, 0, TILE, TILE]))])

    def test_same_shape_with_other_texels_is_planned_not_an_alias(self):
        _, st = self.fx.plan('p', [self.fx.page('a', {(0, 0): 1}),
                                   self.fx.page('b', {(16, 0): 2})])
        self.assertEqual(planned(st), [('a', PAL, [0, 0, TILE, TILE]), ('b', PAL, [16, 0, TILE, TILE])])
        self.assertEqual(len(st['pieces']), 2)
        self.assertEqual(st['aliases'], [])

    def test_tile_of_an_earlier_plan_given_with_reuse_is_an_alias_into_that_plan(self):
        # the earlier plan repaints two same-shaped tiles; the new page repeats the second one
        early, st0 = self.fx.plan('early', [self.fx.page('a', {(0, 0): 1, (16, 16): 2})])
        self.assertEqual(len(st0['pieces']), 2)
        _, st = self.fx.plan('late', [self.fx.page('c', {(0, 16): 2})], reuse=[early])
        self.assertEqual(st['pieces'], [])
        self.assertEqual(st['canvases'], [])
        self.assertEqual(st['aliases'], [dict(page='c', palette=PAL, tile=[0, 16, TILE, TILE],
                                              of=dict(page='a', palette=PAL, tile=[16, 16, TILE, TILE],
                                                      plan=str(early.resolve())))])


# the colour gate (xg-jqe8): a repaint keeps the original's brightness, colours and soft light;
# blank areas stay blank
class TextureGateTests(unittest.TestCase):
    def plan(self, tmp, paint):
        """One canvas, one 96x96-texel piece: a stone wall (two tones in 4-texel blocks) under a soft round light, a
        blank white strip at the bottom. The try is the original at 8x with paint(rgb array) applied."""
        d = Path(tmp) / 'x-redraw'; (d / 'canvases').mkdir(parents=True); (d / 'raw').mkdir()
        S, N = ftr.S, ftr.CANVAS
        w = h = 96; ax = ay = 16
        yy, xx = np.mgrid[0:h, 0:w]
        light = 40 * np.exp(-((xx - 48) ** 2 + (yy - 40) ** 2) / (2 * 22 ** 2))
        stone = np.where(((xx // 4) + (yy // 4)) % 2, 96, 110)[..., None] * np.array([1.0, 0.95, 0.85])
        tex = np.clip(stone + light[..., None], 0, 255)
        tex[84:] = 255   # the blank strip
        inp = np.full((N * S, N * S, 3), 128, np.uint8)
        inp[ay * S:(ay + h) * S, ax * S:(ax + w) * S] = np.repeat(np.repeat(tex, S, 0), S, 1).astype(np.uint8)
        mask = np.zeros((N * S, N * S), np.uint8); mask[ay * S:(ay + h) * S, ax * S:(ax + w) * S] = 255
        Image.fromarray(inp).save(d / 'canvases' / '001.input.png'); Image.fromarray(mask).save(d / 'canvases' / '001.mask.png')
        Image.fromarray(np.clip(paint(inp.astype(np.float32)), 0, 255).astype(np.uint8)).save(d / 'raw' / '001.try1.png')
        st = dict(pieces=[dict(page='p', palette=0, box=[0, 0, w, h], at=[ax, ay])],
                  canvases=[dict(name='001', pieces=[0], done=False)])
        (d / 'plan.json').write_text(json.dumps(st))
        return d, st

    def check(self, paint):
        with tempfile.TemporaryDirectory() as tmp:
            d, st = self.plan(tmp, paint)
            return ftr.texture_check(d, st, st['canvases'][0], d / 'raw' / '001.try1.png')

    def test_the_original_passes(self):
        self.assertTrue(self.check(lambda a: a)['ok'])

    def test_a_brighter_room_is_refused(self):
        r = self.check(lambda a: a * 1.35)
        self.assertFalse(r['ok']); self.assertGreater(r['dL'], ftr.TEX_DL)
        self.assertIn('lighter', '\n'.join(r['faults']))

    def test_a_colour_shift_is_refused_and_described(self):
        r = self.check(lambda a: a * np.array([0.7, 0.9, 1.3]))
        self.assertFalse(r['ok']); self.assertGreater(r['dab'], ftr.TEX_DAB)
        self.assertIn('was painted', '\n'.join(r['faults']))

    def test_painting_the_blank_strip_is_refused(self):
        def paint(a):
            a = a.copy(); a[(16 + 84) * ftr.S:(16 + 96) * ftr.S, 16 * ftr.S:112 * ftr.S] = 90
            return a
        r = self.check(paint)
        self.assertFalse(r['ok']); self.assertGreater(r['painted'], ftr.TEX_BLANK)

    def test_a_piece_one_texel_wide_is_measured(self):
        with tempfile.TemporaryDirectory() as tmp:
            d, st = self.plan(tmp, lambda a: a)
            st['pieces'][0]['box'] = [0, 0, 1, 40]
            self.assertTrue(ftr.texture_check(d, st, st['canvases'][0], d / 'raw' / '001.try1.png')['ok'])

    def test_one_refused_try_ingests_nothing_and_writes_the_fix(self):
        with tempfile.TemporaryDirectory() as tmp:
            d, st = self.plan(tmp, lambda a: a * 1.35)
            with self.assertRaises(SystemExit) as e:
                ftr.ingest(SimpleNamespace(dir=str(d), canvas='1', accept_colours=False))
            self.assertEqual(e.exception.code, 3)
            self.assertTrue((d / 'canvases' / '001.colour-fix.txt').exists())
            c = json.loads((d / 'plan.json').read_text())['canvases'][0]
            self.assertFalse(c['done']); self.assertIn('001.try1.png', c['colour_refused'])


if __name__ == '__main__':
    unittest.main()
