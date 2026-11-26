"""w3clouds.py -- The Witcher 3's clear-weather clouds' images for the renderer's r_sceneW3Clouds (Beauclair).

The clouds (dlc/bob/data/fx/environment/weather/clouds/bob_mid_clouds_small_1.w2p: cumulus sprites, a cirrus dome, two
panorama rings; the system, its meshes and its materials are read into scene.frag w3Clouds' constants, see its comment)
draw from three images, packed here from the game's textures:
    textures/skies/w3clouds_sprites.png  clouds_bob_soft_1 (the 4x4 atlas, BC5): r its shade, g its alpha;
                                         b cloud_cover_tiled's r, tiled 2x1 (the cirrus' coverage)
    textures/skies/w3clouds_pano.png     panorama_4 (BC5): r, g likewise; b clouds_noise, tiled 4x1 (the dissolve)
    textures/skies/w3clouds_cirrus.png   bob_cirrus_clouds: rgb its normal map, a its density

Two steps. The textures sit in the game's texture.cache, which only the Blender add-on reads:
    blender -b --python tools/levelport/w3clouds.py -- --extract DIR
writes the .dds files to DIR ($W3_GAME as for w3env.py); then, in plain Python (Pillow reads BC1-5):
    python tools/levelport/w3clouds.py --pack DIR --out zz_w3_clouds.pk3"""
import argparse
import os
import sys

CLOUDS = 'dlc/bob/data/fx/environment/weather/clouds/'
TEXTURES = {
    'clouds_bob_soft_1': CLOUDS + 'clouds_bob_soft_1.xbm',
    'panorama_4': CLOUDS + 'panorama_4.xbm',
    'bob_cirrus_clouds': CLOUDS + 'bob_cirrus_clouds.xbm',
    'clouds_noise': CLOUDS + 'clouds_noise.xbm',
    'cloud_cover_tiled': 'fx/textures/cloud/cloud_cover_tiled.xbm',
}


def extract(out):
    import importlib
    tcm = importlib.import_module('bl_ext.user_default.witcher3_tools.CR2W.witcher_cache.TextureCache')
    manager = tcm.LoadTextureManager(loadmods=False)
    os.makedirs(out, exist_ok=True)
    bad = []
    for name, depot in TEXTURES.items():
        items = manager.find_item_by_path_name(depot.replace('/', '\\').lower())
        if not items:
            bad.append(depot)
            continue
        (items[-1] if isinstance(items, list) else items).extract_to_file(os.path.join(out, name + '.dds'))
    print('W3CLOUDS extract', out, 'missing', bad, flush=True)
    return 3 if bad else 0


def tiled(img, w, h):
    """img repeated to w x h (whole tiles: the result wraps as the source does)"""
    from PIL import Image
    out = Image.new(img.mode, (w, h))
    for y in range(0, h, img.height):
        for x in range(0, w, img.width):
            out.paste(img, (x, y))
    return out


def pack(src, out):
    from PIL import Image
    import io
    import zipfile

    def chan(name, i):
        return Image.open(os.path.join(src, name + '.dds')).convert('RGBA').split()[i]

    sp = Image.open(os.path.join(src, 'clouds_bob_soft_1.dds')).convert('RGB')
    cover = chan('cloud_cover_tiled', 0)
    sprites = Image.merge('RGB', (sp.split()[0], sp.split()[1],
                                  tiled(cover.resize((sp.width // 2, sp.height), Image.BICUBIC), sp.width, sp.height)))
    pa = Image.open(os.path.join(src, 'panorama_4.dds')).convert('RGB')
    noise = chan('clouds_noise', 0)
    pano = Image.merge('RGB', (pa.split()[0], pa.split()[1],
                               tiled(noise.resize((pa.width // 4, pa.height), Image.BICUBIC), pa.width, pa.height)))
    cirrus = Image.open(os.path.join(src, 'bob_cirrus_clouds.dds')).convert('RGBA')
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_STORED) as z:
        for name, img in (('sprites', sprites), ('pano', pano), ('cirrus', cirrus)):
            b = io.BytesIO()
            img.save(b, 'PNG', optimize=True)
            z.writestr('textures/skies/w3clouds_%s.png' % name, b.getvalue())
            print('W3CLOUDS', name, img.size, img.mode, len(b.getvalue()), flush=True)
    print('W3CLOUDS wrote', out)
    return 0


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--extract', help='(in Blender) write the game textures as .dds into this folder')
    ap.add_argument('--pack', help='the folder of .dds files to pack')
    ap.add_argument('--out', help='the .pk3 to write (--pack)')
    a = ap.parse_args(argv)
    if a.extract:
        return extract(a.extract)
    if a.pack and a.out:
        return pack(a.pack, a.out)
    ap.error('--extract DIR, or --pack DIR --out PK3')


if __name__ == '__main__':
    rc = main(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:])
    if '--' not in sys.argv:
        sys.exit(rc)
