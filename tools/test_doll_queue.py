"""Recovery preserves art, dependency order and explicit retry authorization (xg-a0u)."""
import hashlib
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import chapter_prep
import doll_queue as queue
REAL_PROMPTS = chapter_prep.cmd_prompts


class QueueTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.setdir = Path(self.tmp.name) / 'battlers-npcs'
        self.setdir.mkdir()
        (self.setdir / 'parked').mkdir()
        (self.setdir / 'dolls.txt').write_text('g301|one|one|character\n')
        (self.setdir / 'parked/dolls.txt').write_text('g304|four|four|character\ng305|five|five|character\ng311|eleven|eleven|character\n')
        (self.setdir / 'extract.tsv').write_text('g301\tsource\ng304\tsource\ng305\tsource\ng311\tsource\n')
        (self.setdir / 'PROMPT.md').write_text('# Battlers: paper dolls, every part drawn once\n')
        (self.setdir / 'after.json').write_text('{}')
        for g in ('g304', 'g305', 'g311'):
            d = self.setdir / 'parked' / g / 'doll'
            (d / 'raw').mkdir(parents=True)
            (d / 'cells').mkdir()
            (d / 'pages.json').write_text(json.dumps({'pages': [{'name': 'wave1-01', 'done': True}, {'name': 'wave2-01'}]}))
            (d / 'cells/a.png').write_bytes(b'locked cells')
            (d / 'raw/wave1-01.try1.png').write_bytes(b'accepted raw')
            (d / 'raw/wave2-01.try1.png').write_bytes(b'failed raw')
        self.reuse = patch.object(chapter_prep, 'cmd_reuse').start()
        self.prompts = patch.object(chapter_prep, 'cmd_prompts').start()
        self.addCleanup(patch.stopall)

    def settle(self, restore=False):
        return queue.settle(self.setdir, restore)

    def test_queue_does_not_move_active_work_and_is_idempotent(self):
        queue.queue(self.setdir, ['g304', 'g305', 'g311'])
        first = (self.setdir / 'parked/queue.json').read_bytes()
        queue.queue(self.setdir, ['g304', 'g305', 'g311'])
        self.assertEqual(first, (self.setdir / 'parked/queue.json').read_bytes())
        self.assertFalse((self.setdir / 'g305').exists())
        self.prompts.assert_not_called()

    def test_restore_keeps_completed_pages_cells_and_raw_and_archives_failed_masters(self):
        original = self.setdir / 'parked/g305/doll'
        hashes = {p.relative_to(original): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in original.rglob('*') if p.is_file() and 'wave2-01' not in p.name}
        queue.queue(self.setdir, ['g311', 'g305', 'g304'])
        self.assertCountEqual(self.settle(True), ['g304', 'g305', 'g311'])
        active = self.setdir / 'g305/doll'
        for path, digest in hashes.items():
            self.assertEqual(hashlib.sha256((active / path).read_bytes()).hexdigest(), digest)
        self.assertFalse((active / 'raw/wave2-01.try1.png').exists())
        archived = list((self.setdir / 'g305/rejected-tries').glob('*/wave2-01.try1.png'))
        self.assertEqual([p.read_bytes() for p in archived], [b'failed raw'])
        self.assertEqual(list(queue.rows(self.setdir / 'dolls.txt')), ['g301', 'g304', 'g305', 'g311'])
        self.assertTrue(self.prompts.call_args.args[0].dolls_only)
        self.assertEqual(self.settle(True), [])

    def test_block_parks_dependents_and_needs_another_explicit_queue(self):
        queue.queue(self.setdir, ['g304', 'g311'])
        self.settle(True)
        (self.setdir / 'after.json').write_text(json.dumps({'g311': 'g304'}))
        (self.setdir / 'blocked').mkdir()
        (self.setdir / 'blocked/g304.json').write_text(json.dumps({'page': 'wave2-01', 'kind': 'quality', 'reason': 'face on back'}))
        self.assertCountEqual(self.settle(), ['g304', 'g311'])
        self.assertEqual(list(queue.rows(self.setdir / 'dolls.txt')), ['g301'])
        self.assertEqual(self.settle(True), [])
        self.assertFalse((self.setdir / 'g304').exists())
        self.assertTrue(list((self.setdir / 'parked/reports').glob('g304-*.json')))
        queue.queue(self.setdir, ['g304', 'g311'])
        self.assertCountEqual(self.settle(True), ['g304', 'g311'])
        self.assertEqual(self.settle(), [])  # the old failure cannot re-park a new retry

    def test_bad_request_does_not_partially_queue_or_overwrite(self):
        for g in ('../g304', 'g999'):
            with self.assertRaises(ValueError):
                queue.queue(self.setdir, ['g304', g])
        self.assertFalse((self.setdir / 'parked/queue.json').exists())
        (self.setdir / 'g304').mkdir()
        with self.assertRaises(ValueError):
            queue.queue(self.setdir, ['g304'])

    def test_failed_prompt_refresh_is_resumed_without_losing_the_restored_table(self):
        queue.queue(self.setdir, ['g305'])
        self.prompts.side_effect = RuntimeError('interrupted')
        with self.assertRaises(RuntimeError):
            self.settle(True)
        self.prompts.side_effect = None
        self.settle(True)
        self.assertIn('g305', queue.rows(self.setdir / 'dolls.txt'))
        self.assertNotIn('_refresh', json.loads((self.setdir / 'parked/queue.json').read_text()))

    def test_restore_resumes_after_a_rename_before_the_receipt_was_saved(self):
        queue.queue(self.setdir, ['g305'])
        (self.setdir / 'parked/g305').rename(self.setdir / 'g305')
        self.assertEqual(self.settle(True), ['g305'])
        self.assertIn('g305', queue.rows(self.setdir / 'dolls.txt'))
        self.assertEqual(self.settle(True), [])

    def test_doll_prompt_refresh_does_not_change_texture_prompts(self):
        root = Path(self.tmp.name)
        (root / 'work/art').mkdir(parents=True)
        (root / 'work/art/GPT-PARALLEL.txt').write_text('# Doll batches:\nDraw whole dolls.\n# Texture batches:\nDraw textures.\n')
        tex = root / 'textures'
        (tex / 'battlers-redraw').mkdir(parents=True)
        (tex / 'battlers-redraw/plan.json').write_text('{}')  # would fail if the doll refresh read it
        launch = tex / 'GPT-BATTLERS.txt'
        launch.write_bytes(b'original texture prompt')
        with patch.object(chapter_prep, 'ROOT', root), patch.object(chapter_prep, 'SETS', root), patch.object(chapter_prep, 'TEX', tex):
            REAL_PROMPTS(SimpleNamespace(name='battlers', title='Battlers', subjects='characters', note='', dolls_only=True))
        self.assertEqual(launch.read_bytes(), b'original texture prompt')
        self.assertIn('Blocked doll workflow', (root / 'GPT-BATTLERS-NPCS.txt').read_text())


if __name__ == '__main__':
    unittest.main()
