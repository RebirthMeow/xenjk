import struct, unittest, zipfile
from pathlib import Path
import strafe_map_preparation as m

def tile():
    h=struct.pack('<5iI9i10f',m.DNAV,m.NAV_VERSION,0,0,0,0,1,3,0,1,0,1,0,0,1,2,0.5,0.25,0,0,0,1,1,1,1)
    verts=struct.pack('<9f',0,0,0,1,0,0,0,0,1)
    poly=struct.pack('<I6H6HHBB',0,0,1,2,0,0,0,0,0,0,0,0,0,1,3,0)
    detail=struct.pack('<IIBB2x',0,0,0,1);tri=bytes((0,1,2,0))
    return h+verts+poly+detail+tri

def bsp(body):
    header=bytearray(8+18*8);header[:8]=b'RBSP'+struct.pack('<i',1)
    for i in range(18):struct.pack_into('<ii',header,8+i*8,len(header),0)
    struct.pack_into('<ii',header,8,len(header),len(body))
    return bytes(header)+body

class Test(unittest.TestCase):
    def test_solo_triangle_and_axis_conversion(self):
        got=m.parse_navmesh(tile());self.assertEqual(got['ground_polygon_count'],1);self.assertEqual(len(got['boundary_segments']),3)
        q=m.quake((1,2,3));self.assertEqual(q,[m.METERS_TO_QUAKE,3*m.METERS_TO_QUAKE,2*m.METERS_TO_QUAKE])
        for a,b in zip(m.recast(q),(1,2,3)):self.assertAlmostEqual(a,b)
    def test_mset_and_truncation(self):
        t=tile();params=struct.pack('<5f2i',0,0,0,1,1,1,1)
        raw=struct.pack('<III',m.MSET,1,1)+params+struct.pack('<Ii',1,len(t))+t
        self.assertEqual(m.parse_navmesh(raw)['tile_count'],1)
        for bad in (raw[:-1],b'bad',t[:-4]):
            with self.assertRaises(ValueError):m.parse_navmesh(bad)
        duplicate=struct.pack('<III',m.MSET,1,2)+struct.pack('<5f2i',0,0,0,1,1,2,16)+struct.pack('<Ii',1,len(t))+t+struct.pack('<Ii',1,len(t))+t
        with self.assertRaisesRegex(ValueError,'duplicate'):m.parse_navmesh(duplicate)
    def test_bad_polygon_index(self):
        raw=bytearray(tile());struct.pack_into('<H',raw,100+36+4,99)
        with self.assertRaises(ValueError):m.parse_navmesh(raw)
    def test_bsp_entities(self):
        body=b'{\n"classname" "team_CTF_redflag"\n"origin" "1 2 3"\n}\x00'
        self.assertEqual(m.bsp_entities(bsp(body))[0]['origin'],[1,2,3])
        with self.assertRaises(ValueError):m.bsp_entities(b'RBSP')
        bad=bytearray(bsp(body));struct.pack_into('<i',bad,8,16)
        with self.assertRaisesRegex(ValueError,'lump bounds'):m.bsp_entities(bad)
    def test_nonfinite_and_polygon_shape_rejected(self):
        raw=bytearray(tile());struct.pack_into('<f',raw,60,float('nan'))
        with self.assertRaisesRegex(ValueError,'non-finite'):m.parse_navmesh(raw)
        raw=bytearray(tile());struct.pack_into('<f',raw,64,-1)
        with self.assertRaisesRegex(ValueError,'walkability'):m.parse_navmesh(raw)
        raw=bytearray(tile());struct.pack_into('<f',raw,100,float('nan'))
        with self.assertRaisesRegex(ValueError,'vertex'):m.parse_navmesh(raw)
        raw=bytearray(tile());raw[100+36+30]=2
        with self.assertRaisesRegex(ValueError,'vertex count'):m.parse_navmesh(raw)
        raw=bytearray(tile());raw[100+36+31]=128
        with self.assertRaisesRegex(ValueError,'polygon type'):m.parse_navmesh(raw)
    def test_repository_kejim_and_dantooine_reparse(self):
        root=Path(__file__).resolve().parent.parent
        cases=[('ctf_kejim.pk3','maps/mp/ctf_kejim.bsp','navmesh example files/ctf_kejim.navmesh',539),
               ('ctf_dantooine_v3.pk3','maps/ctf_dantooine_v3.bsp','game_directory/Jedi Academy/GameData/base/maps/ctf_dantooine_v3.navmesh',2404)]
        base=root/'game_directory/Jedi Academy/GameData/base'
        missing=[str(p) for p in [base/c[0] for c in cases]+[root/c[2] for c in cases] if not p.exists()]
        if missing:self.skipTest('needs the installed game and navmeshes: '+', '.join(missing))
        for package,member,nav,count in cases:
            with zipfile.ZipFile(base/package) as archive:bspdata=archive.read(member)
            self.assertTrue(m.bsp_entities(bspdata))
            self.assertEqual(m.parse_navmesh((root/nav).read_bytes())['ground_polygon_count'],count)

if __name__=='__main__':unittest.main()
