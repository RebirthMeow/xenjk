"""CP2077 2.5 sectors (WolvenKit 10's layout) rewritten in the 2.31 layout the readers know (cp77_sector25.py)."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / 'levelport'))
import cp77_sector25 as s25  # noqa: E402


def mesh(path):
    return {'DepotPath': {'$value': path}}


def rec(ni, ii, x=1.0):
    return {'$type': 'worldCompiledNodeInstanceSetupInfo', 'nodeIdx': str(ni), 'instanceIndex': ii,
            'transform': {'position': {'X': x, 'Y': 2.0, 'Z': 3.0}, 'orientation': {'r': 1}},
            'scale': {'X': 1, 'Y': 1, 'Z': 1}, 'streamingDistance': 92.5, 'secondaryRefPointDistance': 70.0,
            'streamingRefPoint': {'X': 0, 'Y': 0, 'Z': 0}}


def sector():
    ad = {'$type': 'worldAdvertisementLightData', 'color': {'Red': 255, 'Green': 255, 'Blue': 255, 'Alpha': 255}}
    static = {'Data': {'$type': 'worldCookedStaticMeshNode', 'debugName': 'n', 'lightsData': [
        ad, dict(ad, color={'Red': 255, 'Green': 58, 'Blue': 117, 'Alpha': 255})], 'buffer': {'Data': {'InnerNodes': [
        {'$type': 'WorldCookedGenericProxyMeshInnerNode', 'mesh': mesh('a.mesh'), 'forcedAutoHideDistance': 80},
        {'$type': 'WorldCookedGenericProxyMeshInnerNode', 'mesh': mesh('b.mesh'), 'forcedAutoHideDistance': 40}]}}}}
    inst = {'Data': {'$type': 'worldInstancedMeshNode', 'buffer': {'Data': {
        'InnerNodes': [{'mesh': mesh('c.mesh'), 'forcedAutoHideDistance': 50}],
        'TransformRanges': [{'transformIndex': 0, 'transformCount': 1}]}},
        'cookedWorldTransformsBuffer': {'Data': {'Transforms': [{'scale': {'X': 2, 'Y': 1, 'Z': 1}, 'transform': {
            'Orientation': {'r': 1}, 'Position': {'x': {'Bits': -168653664}, 'y': {'Bits': 131072}, 'z': {'Bits': 0}}}}]}}}}
    decal = {'Data': {'$type': 'worldCookedStaticDecalNode', 'buffer': {'Data': {
        'InnerNodes': [{'material': mesh('d.mi'), 'flags': 'VerticalFlip, EnableNormalTreshold', 'diffuseColorScaleIndex': 0}],
        'DiffuseColorScales': [{'X': 0.5}]}}}}
    dest = {'Data': {'$type': 'worldInstancedDestructibleMeshNode', 'mesh': mesh('e.mesh'),
                     'instanceTransforms': [{'position': {'X': 0}, 'orientation': {'r': 1}}]}}
    dev = {'Data': {'$type': 'worldDeviceNode', 'instanceData': {'Data': {'buffer': {'Data': {'Chunks': [
        {'$type': 'gameLightComponent', 'color': {'Red': 255, 'Green': 255, 'Blue': 255, 'Alpha': 255}},
        {'$type': 'gameLightComponent', 'color': {'Red': 255, 'Green': 120, 'Blue': 0, 'Alpha': 255}}]}}}}}}
    return {'nodes': [static, inst, decal, dest, dev],
            'nodeData': {'Data': [rec(0, 1), rec(0, 0, 5.0), rec(1, 0), rec(2, 0), rec(3, 4294967295), rec(4, 4294967295)]}}


class Sector25(unittest.TestCase):
    def setUp(self):
        self.rc = sector()
        self.stats = s25.legacy_root(self.rc)
        self.nodes = [n['Data'] for n in self.rc['nodes']]
        self.recs = self.rc['nodeData']['Data']

    def node(self, i):
        return self.nodes[self.recs[i]['NodeIndex']]

    def test_records_keep_their_order_and_fields(self):
        self.assertEqual(len(self.recs), 6)
        self.assertEqual(self.recs[1]['Position']['X'], 5.0)
        self.assertEqual(self.recs[0]['UkFloat1'], 92.5)
        self.assertEqual(self.recs[0]['MaxStreamingDistance'], 70.0)

    def test_each_record_gets_its_own_inner_node(self):
        self.assertEqual(self.node(0)['$type'], 'worldStaticMeshNode')
        self.assertEqual(self.node(0)['mesh']['DepotPath']['$value'], 'b.mesh')
        self.assertEqual(self.node(1)['mesh']['DepotPath']['$value'], 'a.mesh')
        self.assertEqual(self.node(0)['forceAutoHideDistance'], 40)
        self.assertEqual(self.node(0)['debugName'], 'n')

    def test_instanced_transforms_come_back_inline_in_metres(self):
        n = self.node(2)
        T = n['worldTransformsBuffer']['sharedDataBuffer']['Data']['buffer']['Data']['Transforms']
        self.assertAlmostEqual(T[0]['translation']['X'], -1286.7253, places=3)
        self.assertAlmostEqual(T[0]['translation']['Y'], 1.0)
        self.assertEqual(n['buffer'], {'Unknown2': [50], 'Unknown3': [1]})

    def test_decal_flags_and_colour(self):
        n = self.node(3)
        self.assertEqual((n['verticalFlip'], n['horizontalFlip'], n['enableNormalTreshold']), (1, 0, 1))
        self.assertEqual(n['diffuseColorScale'], {'X': 0.5})

    def test_white_instance_lights_are_placeholders_again(self):
        ch = self.node(5)['instanceData']['Data']['buffer']['Data']['Chunks']
        self.assertEqual(ch[0]['color']['Red'], 0)
        self.assertEqual(ch[0]['color']['Alpha'], 0)
        self.assertEqual(ch[1]['color']['Green'], 120)

    def test_white_ad_lights_are_placeholders_again(self):
        L = self.node(0)['lightsData']
        self.assertEqual((L[0]['color']['Red'], L[0]['color']['Alpha']), (0, 0))
        self.assertEqual(L[1]['color']['Green'], 58)
        self.assertEqual(self.stats['placeholder ad lights'], 1)

    def test_destructible_transforms_and_marks(self):
        self.assertEqual(self.node(4)['cookedInstanceTransforms']['numElements'], 1)
        self.assertEqual(self.rc['xenjkLegacyFrom'], '2.5')
        self.assertFalse(s25.is_25(self.rc))
        self.assertEqual(s25.legacy_root(self.rc), {})


class Upgrade(unittest.TestCase):
    def test_a_version_3_file_gets_the_ad_lights_only(self):
        rc = {'xenjkLegacyFrom': '2.5', 'xenjkLegacyVersion': 3, 'nodes': [{'Data': {
            '$type': 'worldStaticMeshNode', 'lightsData': [{'color': {'Red': 255, 'Green': 255, 'Blue': 255, 'Alpha': 255}}]}},
            {'Data': {'$type': 'worldDeviceNode', 'instanceData': {'Data': {'buffer': {'Data': {'Chunks': [
                {'$type': 'gameLightComponent', 'color': {'Red': 255, 'Green': 255, 'Blue': 255, 'Alpha': 255}}]}}}}}}],
            'nodeData': {'Data': []}}
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / 's.streamingsector.json'
            f.write_text(json.dumps({'Data': {'RootChunk': rc}}), encoding='utf-8')
            self.assertEqual(s25.convert_file(f), {'placeholder ad lights': 1, 'upgraded': 1})
            self.assertEqual(s25.convert_file(f), {})             # (a second run leaves it)
            out = json.loads(f.read_text(encoding='utf-8'))['Data']['RootChunk']
        self.assertEqual(out['xenjkLegacyVersion'], s25.VERSION)
        self.assertEqual(out['nodes'][0]['Data']['lightsData'][0]['color']['Red'], 0)
        self.assertEqual(out['nodes'][1]['Data']['instanceData']['Data']['buffer']['Data']['Chunks'][0]['color']['Red'], 255)


class AppHash(unittest.TestCase):
    def test_a_hashed_appearance_finds_its_mesh_appearance(self):
        import cp2gltf

        class M:
            apps = {'default': ['a'], 'cyan': ['b']}
        M.app_name = cp2gltf.MeshLods.app_name
        M.material_names = cp2gltf.MeshLods.material_names
        self.assertEqual(cp2gltf.cname_hash('cyan'), 8119478377456132730)
        self.assertEqual(M().material_names('8119478377456132730'), ['b'])
        self.assertEqual(M().material_names('cyan'), ['b'])
        self.assertEqual(M().material_names('12345'), ['a'])


class Resource25(unittest.TestCase):
    def test_white_template_lights_become_placeholders_only_from_wolvenkit_10(self):
        light = {'$type': 'gameLightComponent', 'color': {'Red': 255, 'Green': 255, 'Blue': 255, 'Alpha': 255}}
        with tempfile.TemporaryDirectory() as d:
            for ver, want in (('10.0.0-beta.1', 0), ('9.0.1', 255)):
                f = Path(d) / ('%s.ent.json' % ver)
                f.write_text(json.dumps({'Header': {'WolvenKitVersion': ver}, 'Data': {'RootChunk': {'components': [
                    dict(light, color=dict(light['color']))]}}}), encoding='utf-8')
                s25.convert_resource_file(f)
                s25.convert_resource_file(f)          # (a second run leaves it)
                c = json.loads(f.read_text(encoding='utf-8'))['Data']['RootChunk']['components'][0]['color']
                self.assertEqual(c['Red'], want)


if __name__ == '__main__':
    unittest.main()
