"""The runs clause of a chapter's row comes from what its runs recorded (xg-xsxy): seven art-prep rounds in a day were
rejected because a hand-written row or count did not match the runner's facts (a run mostly in another field, a party
the spec did not ask for, 22 runs written where 21 were kept)."""
from pathlib import Path
import argparse
import contextlib
import importlib.util
import io
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parent


def chapter_prep():
    spec = importlib.util.spec_from_file_location('chapter_prep', TOOLS / 'chapter_prep.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PRELOAD = 'sprite preload: field {field} npc -1 (disc 1): 9 parts ({n} party dolls: {party})\n'


class RowTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.cp = chapter_prep()
        self.cp.ROOT = Path(self.tmp.name)
        self.cp.TEX = self.cp.ROOT / 'work/art/field-tex'; self.cp.SETS = self.cp.ROOT / 'work/art/field-set'
        self.D = self.cp.TEX / 'demo'
        (self.D / 'logs').mkdir(parents=True)
        self.specs = []

    def run_(self, name, spec, samples, loads=None, quit_=True):
        """A kept run: `samples` {field or -1: count}, `loads` [(field, 'a b c')] (default: the spec's field and party)."""
        field, _, party = spec.split()[:3]
        self.specs.append(f'{name} {spec}')
        (self.D / 'specs.txt').write_text('# demo\n' + '\n'.join(self.specs) + '\n')
        out = self.D / 'dumps' / name / 'SLUS-00664'; out.mkdir(parents=True)
        (out / 'samples.jsonl').write_text(''.join(f'{{"field":{f}}}\n' * n for f, n in samples.items()))
        loads = loads if loads is not None else [(field, ' '.join('-1' if p == '255' else p for p in party.split(',')))]
        (self.D / 'logs' / f'{name}.log').write_text(
            ''.join(PRELOAD.format(field=f, n=len(p.split()), party=p) for f, p in loads) + ('xenogears: quit\n' if quit_ else 'still drawing\n'))

    def clause(self):
        return self.cp.runs_clause(self.cp.chapter_runs('demo', None)[0])

    def test_clean_runs_say_so_in_one_phrase(self):
        self.run_('f10-p1', '10 1 0,1,2 0 4000', {10: 90, 11: 10})
        self.run_('f11-p1', '11 1 0,255,255 0 4000', {11: 50})
        self.assertEqual(self.clause(), '[runs: 2, all quit by themselves; every run has at least half its samples in the field asked for; '
                                        'party as asked in every load]')

    def test_a_run_mostly_in_another_field_or_in_none_is_counted_against_its_field(self):
        self.run_('f10-p1', '10 1 0,1,2 0 4000', {10: 90})
        self.run_('f10-p2', '10 2 0,1,2 0 4000', {10: 2282, 46: 2000})       # 2282 of 4282: over half in its own field, so not counted
        self.run_('f12-p1', '12 1 0,1,2 0 4000', {12: 895, -1: 6279})        # mostly outside any field (xg-pse.21's f368)
        self.run_('f13-p1', '13 1 0,1,2 0 4000', {14: 7187, 13: 6390})       # mostly the next room (xg-pse.17's f351)
        self.assertIn('under half their samples in the field asked for: 12 1 of 1 runs, 13 1 of 1;', self.clause())
        self.assertNotIn(' 10 ', self.clause().split('asked for:')[1])

    def test_a_party_the_spec_did_not_ask_for_is_named_with_its_field(self):
        # xg-pse.19: f280-p43 asked 0,2,255, kept it in 280, but the game loaded 3 2 -1 in 287
        self.run_('f280-p43', '280 43 0,2,255 0 4000', {280: 60, 287: 40}, loads=[('280', '0 2 -1'), ('287', '3 2 -1')])
        self.assertIn('party not as asked: f280-p43 3 2 -1 in 287]', self.clause())

    def test_a_run_that_did_not_quit_and_a_full_table_are_named(self):
        self.run_('f10-p1', '10 1 0,1,2 0 4000', {10: 5})
        self.run_('f10-p2', '10 2 0,1,2 0 4000', {10: 5}, quit_=False)
        self.cp.SAMPLES_FULL = 5
        clause = self.clause()
        self.assertIn('runs: 2, 1 quit by themselves (not f10-p2); sample table full: f10-p1, f10-p2;', clause)

    def test_the_check_passes_on_the_generated_clause_and_fails_on_a_typed_one(self):
        self.run_('f10-p1', '10 1 0,1,2 0 4000', {10: 10, 11: 90})
        runs = self.cp.chapter_runs('demo', None)[0]
        chapters = self.cp.ROOT / 'ART_CHAPTERS.md'
        row = '| demo | 10 | - | 1 requested | 0,1,2 | 0/4 | prepared (part 1a; {}) |\n'
        chapters.write_text('# chapters\n' + row.format(self.cp.runs_clause(runs)))
        self.assertTrue(self.cp.row_check('demo', runs).startswith('ok'))
        chapters.write_text('# chapters\n' + row.format('[runs: 1, all quit by themselves; every run has at least half its samples in the field asked for; party as asked in every load]'))
        self.assertTrue(self.cp.row_check('demo', runs).startswith('DIFFERS'), self.cp.row_check('demo', runs))
        chapters.write_text('# chapters\n' + row.format('1 run quit by itself, partly in 11'))
        self.assertTrue(self.cp.row_check('demo', runs).startswith('MISSING'))
        chapters.write_text('# chapters\n')
        self.assertIn('no row for demo', self.cp.row_check('demo', runs))

    def test_the_report_line_counts_the_kept_runs_and_this_rounds(self):
        self.run_('f10-p1', '10 1 0,1,2 0 4000', {10: 10})
        self.run_('f10-p2', '10 2 0,1,2 0 4000', {10: 10})
        runs = self.cp.chapter_runs('demo', 0.001)[0]   # a round that began before both runs
        self.assertEqual(self.cp.run_line('demo', runs), 'runs of demo: 2 kept in all, 2 made in this round')
        self.assertEqual(self.cp.run_line('demo', self.cp.chapter_runs('demo', None)[0]), 'runs of demo: 2 kept in all, 0 made in this round')


VALIDATED = ('new', 'catalogue', 'atlas', 'plan', 'overview', 'reuse', 'register', 'prompts')
REQUIRED = {'new': ['--title', 't', '--fields', '1'], 'catalogue': ['--fields', '1'], 'register': ['--title', 't', '--prefix', 'p'],
            'prompts': ['--title', 't', '--subjects', 's']}
BAD = ('', '../lair', 'a/b', 'lair/..', '/lair', 'a\\b', '.', '..', '.lair', 'lair.', 'a..b', 'nortune--city', '-lair', 'lair-',
       'Lair', 'lair npcs', 'lair_npcs', 'lair\n')


class NameTests(unittest.TestCase):
    """xg-km5l: a chapter's name becomes a path under work/art and the pack tag, so it is checked before any handler."""
    def setUp(self):
        self.cp = chapter_prep(); self.called = []
        for c in VALIDATED + ('facts', 'row'):
            setattr(self.cp, 'cmd_' + c, lambda a, c=c: self.called.append((c, a.name)))

    def run_main(self, argv):
        with contextlib.redirect_stderr(io.StringIO()) as err:
            try: self.cp.main(argv)
            except SystemExit as e: return e.code, err.getvalue()
        return None, err.getvalue()

    def test_the_validator_takes_dash_joined_words_only(self):
        for good in ('lair', 'nortune-burning-city', 'ch1b', 'room-01', 'party-citan-sword'):
            self.assertEqual(self.cp.chapter_name(good), good)
        for bad in BAD:
            with self.assertRaises(argparse.ArgumentTypeError, msg=repr(bad)): self.cp.chapter_name(bad)

    def test_every_naming_subcommand_refuses_a_bad_name_before_its_handler(self):
        for c in VALIDATED:
            for bad in BAD:
                code, err = self.run_main([c, bad] + REQUIRED.get(c, []))
                self.assertEqual(code, 2, (c, bad))   # a usage error; '-lair' reads as an option, so the name is missing
                self.assertIn('the following arguments are required: name' if bad.startswith('-') else 'is not a chapter name', err, (c, bad))
        self.assertEqual(self.called, [])

    def test_a_good_name_reaches_the_handler_unchanged(self):
        for c in VALIDATED:
            for good in ('lair', 'nortune-burning-city'):
                self.assertEqual(self.run_main([c, good] + REQUIRED.get(c, [])), (None, ''))
        self.assertEqual(self.called, [(c, g) for c in VALIDATED for g in ('lair', 'nortune-burning-city')])

    def test_facts_and_row_are_left_as_they_were(self):
        p = self.cp.parser()
        self.assertEqual(p.parse_args(['facts', 'Lair', 'party_x']).name, ['Lair', 'party_x'])
        self.assertEqual(p.parse_args(['row', 'Lair']).name, 'Lair')


if __name__ == '__main__':
    unittest.main()
