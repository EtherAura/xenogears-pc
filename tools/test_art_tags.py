"""Every texture chapter has a pack tag of its own (xg-a3c): nortune-battlers had nortune-burning-city's 'nb', so the
installed pack's name made it read as installed, and GPT would never have drawn it."""
from pathlib import Path
import importlib.util
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import art_status  # noqa: E402


def chapter_prep():
    spec = importlib.util.spec_from_file_location('chapter_prep', TOOLS / 'chapter_prep.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TagTests(unittest.TestCase):
    def test_installed_chapters_keep_their_tags(self):
        # the installed pack's name is the lineage of these tags: a changed one would read as not installed
        for name, tag in {'nortune-burning-city': 'nb', 'nortune-dock': 'nd', 'goliath-factory': 'gf', 'aveh-castle': 'ac',
                          'bledavik': 'bl', 'flagship': 'fl', 'world': 'w', 'lahan-battle': 'lahan', 'nortune-battlers': 'nbt'}.items():
            self.assertEqual(art_status.tag_of(name), tag, name)

    def test_names_with_the_same_first_initials_get_different_tags(self):
        self.assertNotEqual(art_status.tag_of('nortune-battlers'), art_status.tag_of('nortune-burning-city'))
        self.assertNotEqual(art_status.tag_of('nortune-palace-sewers'), art_status.tag_of('nortune-purge'))

    def test_chapter_prep_names_packs_with_the_tags_art_status_reads(self):
        prep = chapter_prep()
        for name in ('nortune-battlers', 'nortune-palace-sewers', 'cave', 'world'):
            self.assertEqual(prep.tag_of(name), art_status.tag_of(name), name)

    def test_new_refuses_a_name_whose_tag_is_taken(self):
        prep = chapter_prep()
        with tempfile.TemporaryDirectory() as tmp:
            tex = Path(tmp)
            (tex / 'build-nortune-purge.sh').write_text('')
            with patch.object(prep, 'TEX', tex), patch.object(prep, 'installed_pack', lambda: 'lahan-ch1-gf-nd-nb-8x-v21'):
                self.assertEqual(prep.tag_clash('nortune-park'), 'nortune-purge')        # 'np' twice
                self.assertIn('installed pack', prep.tag_clash('nortune-bay'))          # 'nb' is in the installed name
                self.assertEqual(prep.tag_clash('nortune-palace-sewers'), '')           # 'nps'
                with self.assertRaises(SystemExit):
                    prep.cmd_new(SimpleNamespace(name='nortune-park', title='x', fields='1', story=''))
                self.assertFalse((tex / 'nortune-park').exists())                      # refused before anything is made


if __name__ == '__main__': unittest.main()
