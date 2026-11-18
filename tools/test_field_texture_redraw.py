"""field_texture_redraw plan(): a tile with the texels and shape of one already repainted (in this
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


if __name__ == '__main__':
    unittest.main()
