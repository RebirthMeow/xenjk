import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import movement_cases_from_bsp as generate  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
BASE = ROOT / 'game_directory/Jedi Academy/GameData/base'


class KejimTest(unittest.TestCase):
    @unittest.skipUnless((BASE / 'ctf_kejim.pk3').is_file(), 'needs the installed Kejim pk3')
    def test_the_declared_kejim_set_is_reproduced_field_for_field(self):
        doc = generate.build(BASE / 'ctf_kejim.pk3', 'maps/mp/ctf_kejim.bsp', 'mp/ctf_kejim', 'x')
        declared = json.loads((ROOT / 'tools/fixtures/movement/kejim/cases.json').read_text(encoding='utf-8'))
        self.assertEqual(doc['bsp_sha256'], declared['bsp_sha256'])
        self.assertEqual(doc['cases'], declared['cases'])


if __name__ == '__main__':
    unittest.main()
