# ===========================================================================
# Copyright (C) 2026, xenjk contributors
#
# This file is part of xenjk, an OpenJK derivative.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
# ===========================================================================
"""cp77_signals: a placed signal joins its intersection's group program through the FNV-1a64 hash of its listener's
intersectionRef; a signal without a listener has no program; a crossing in the glitch area gets the glitched sounds."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / 'levelport'))
import cp77_signals as cs  # noqa: E402

REF = '$/kabuki/#traffic/#intersection_1'


def rec(i, x, y, z, k=0.0, r=1.0, scale=(1, 1, 1)):
    return {'NodeIndex': i, 'Position': {'X': x, 'Y': y, 'Z': z}, 'Orientation': {'i': 0, 'j': 0, 'k': k, 'r': r},
            'Scale': {'X': scale[0], 'Y': scale[1], 'Z': scale[2]}}


def entity(template, listener=None):
    chunks = [{'$type': 'worldTrafficLightListenerComponent', 'intersectionRef': {'$value': listener[0]},
               'groupIdx': listener[1]}] if listener else []
    return {'Data': {'$type': 'worldEntityNode', 'debugName': {'$value': 'sig'},
                     'entityTemplate': {'DepotPath': {'$value': 'base\\traffic\\' + template}},
                     'appearanceName': {'$value': 'default'},
                     'instanceData': {'Data': {'buffer': {'Data': {'Chunks': chunks}}}}}}


SECTOR = {'Data': {'RootChunk': {
    'nodes': [
        entity('crossing_light_1.ent', (REF, 2)),
        entity('traffic_light_1.ent', (REF, 4)),
        entity('traffic_light_2.ent'),
        {'Data': {'$type': 'worldTrafficCompiledNode', 'debugName': {'$value': 'compiledTraffic_Intersection_1'},
                  'unk1': [{'unk3': str(cs.fnv1a64(REF)), 'unk4': 'AQI=', 'unk5': 1, 'unk6': 3.0, 'unk8': [],
                            'unk7': [{'unk1': 0, 'unk2': 1.0, 'unk3': 0, 'unk4': 2,
                                      'unk5': [{'unk1': 0, 'unk2': 10.0}, {'unk1': 2, 'unk2': 8.333}, {'unk1': 1, 'unk2': 30.0}]},
                                     {'unk1': 0, 'unk2': 0.0, 'unk3': 0, 'unk4': 4,
                                      'unk5': [{'unk1': 1, 'unk2': 20.0}, {'unk1': 0, 'unk2': 25.833}, {'unk1': 2, 'unk2': 2.5}]}]}]}},
        {'Data': {'$type': 'worldAmbientAreaNode', 'debugName': {'$value': cs.GLITCH_AREA},
                  'outline': {'Data': {'height': 2}}}},
    ],
    'nodeData': {'Data': [rec(0, 10.0, 20.0, 5.0), rec(0, 10.0, 20.0, 5.0), rec(1, 12.0, 20.0, 5.0),
                          rec(2, 50.0, 50.0, 5.0), rec(3, 11.0, 21.0, 5.0),
                          rec(4, 10.0, 20.0, 4.0, scale=(5, 5, 3))]},
}}}


class Groups(unittest.TestCase):
    def test_join(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / 'exterior_0_0_0_0.streamingsector.json').write_text(json.dumps(SECTOR), encoding='utf-8')
            (Path(d) / 'exterior_1_0_0_0.streamingsector.json').write_text('{"nothing": 1}', encoding='utf-8')
            sig, ints, glitch = cs.read_sectors(d)
        self.assertEqual(len(sig), 4)                      # the crossing's two records are one signal after dedup
        ents = [{'template': 'base\\traffic\\crossing_light_1.ent', 'm': [1, 0, 0, 10.0, 0, 1, 0, 20.0, 0, 0, 1, 5.0]}]
        doc = cs.build_groups('kabuki', sig, ints, glitch, ents)
        by = {s['template']: s for s in doc['signals']}
        self.assertEqual(len(doc['signals']), 3)
        cross, car, lone = by['crossing_light_1.ent'], by['traffic_light_1.ent'], by['traffic_light_2.ent']
        self.assertEqual(cross['program'], [['GREEN', 10.0], ['YELLOW', 8.333], ['RED', 30.0]])
        self.assertEqual(cross['group_id'], 'compiledTraffic_Intersection_1#2')
        self.assertTrue(cross['in_port_placements'])
        self.assertTrue(cross['audio']['glitch_heavy_area'])
        self.assertTrue(cross['audio']['on_green'].endswith('_glitch_heavy'))
        self.assertEqual(car['program'][0], ['RED', 20.0])
        self.assertIsNone(car['audio'])
        self.assertEqual(doc['intersections']['compiledTraffic_Intersection_1']['groups']['2']['pedestrian_lanes'], 1)
        self.assertIsNone(lone['program'])
        self.assertIn('no listener override', lone['note'])


if __name__ == '__main__':
    unittest.main()
