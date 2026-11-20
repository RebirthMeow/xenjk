"""Build xenjk_reflect: a reflection-probe test hall for rd-vulkan (cubemap parallax correction and probe blending).

A sealed hall in three colour zones (amber, cyan, magenta), one misc_cubemap per zone. The floor is polished dark
metal, the pillars are chrome, and glowing vertical strips line the walls, so reflections are sharp and easy to place:
with parallax correction a strip's reflection stays directly below it as the camera moves; without it the reflection
slides. The floor is laid in 256-unit tiles, so the probe chosen per surface shows as tile-sized jumps in colour.
Own flat textures only; reuses the showcase builder's brush helpers and q3map2 setup.

    python tools/reflection_map/build_reflection_map.py [--out DIR] [--fast]
"""
import argparse
import shutil
import sys
import time
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'showcase_map'))
import build_showcase_map as sm   # noqa: E402  (brush helpers, q3map2 runner)

ROOT = sm.ROOT
NAME = 'xenjk_reflect'
TEX = 'xenjk_reflect'
HALF_X, HALF_Y, H = 1536, 384, 384
ZONES = [(-1536, -512, 'amber', (255, 150, 40)), (-512, 512, 'cyan', (40, 210, 255)), (512, 1536, 'magenta', (255, 50, 200))]
STRIP_LIGHT = 6000
PROBE_RADIUS = 420


def T(name):
    return '%s/%s' % (TEX, name)


def build_map():
    world = []
    # shell: ceiling and outer slabs caulk-backed; the inside faces carry the materials
    world += sm.hollow_box((-HALF_X, -HALF_Y, 0), (HALF_X, HALF_Y, H), 16,
                           {'floor': sm.CAULK, 'ceiling': T('ceiling'), 'xneg': T('end_wall'), 'xpos': T('end_wall'),
                            'yneg': sm.CAULK, 'ypos': sm.CAULK})
    # floor tiles (a 4-unit slab over the caulk floor)
    for x in range(-HALF_X, HALF_X, 256):
        for y in range(-HALF_Y, HALF_Y, 256):
            world.append(sm.box((x, y, 0), (x + 256, y + 256, 4), {'top': T('floor'), 'bottom': sm.CAULK,
                                                                   'side': sm.CAULK}))
    # side walls per zone: a coloured panel wall, glowing vertical strips every 256 units, a skirting band
    for x0, x1, name, _ in ZONES:
        for sgn in (-1, 1):
            y_in = sgn * HALF_Y
            y_out = y_in + sgn * 16
            lo, hi = (y_in, y_out) if sgn > 0 else (y_out, y_in)
            world.append(sm.box((x0, lo, 0), (x1, hi, H), {'top': sm.CAULK, 'bottom': sm.CAULK, 'side': T('wall_' + name)}))
            for xs in range(x0 + 128, x1, 256):
                d = 12   # the strip stands proud of the wall
                slo, shi = (y_in - d, y_in) if sgn > 0 else (y_in, y_in + d)
                world.append(sm.box((xs - 12, slo, 48), (xs + 12, shi, H - 48), T('strip_' + name)))
    # ceiling light bar down the middle
    world.append(sm.box((-HALF_X + 64, -24, H - 8), (HALF_X - 64, 24, H), T('strip_white')))
    # chrome pillars down the centre line, one each side of every probe
    for x in (-1280, -768, -256, 256, 768, 1280):
        for y in (0,):
            world.append(sm.prism(sm.ngon(28, 16, x, y), 4, H, {'top': T('chrome'), 'bottom': T('chrome'),
                                                                     'side': T('chrome'), 'scale': 0.25}))
    ents = [sm.entity('worldspawn', world, message='xenjk reflection probe hall', _ambient='40')]
    for x0, x1, name, _ in ZONES:
        cx = (x0 + x1) / 2
        ents.append(sm.entity('misc_cubemap', origin=sm.v3(cx, 0, 96), name=name, radius=str(PROBE_RADIUS)))
        ents.append(sm.entity('info_player_deathmatch', origin=sm.v3(cx, -100, 24), angle='0'))
    ents.append(sm.entity('info_player_start', origin=sm.v3(-1300, 0, 24), angle='0'))
    return '\n'.join(ents) + '\n'


def material(name, light=None, rgb=None):
    """A lit PBR stage (albedo, flat normal, RMO) under the lightmap; emissive strips add a glow stage and light."""
    head = ['textures/%s/%s' % (TEX, name), '{', '\tqer_editorimage textures/%s/%s_d' % (TEX, name)]
    if light:
        head += ['\tq3map_surfacelight %d' % light, '\tq3map_lightRGB %.2f %.2f %.2f' % rgb, '\tq3map_lightSubdivide 32']
    body = ['\t{', '\t\tmap textures/%s/%s_d' % (TEX, name), '\t\tnormalMap textures/%s/flat_n' % TEX,
            '\t\trmoMap textures/%s/%s_rmo' % (TEX, name), '\t\trgbGen identity', '\t}',
            '\t{', '\t\tmap $lightmap', '\t\tblendFunc GL_DST_COLOR GL_ZERO', '\t\trgbGen identity', '\t}']
    if light:
        body += ['\t{', '\t\tmap textures/%s/%s_d' % (TEX, name), '\t\tblendFunc GL_ONE GL_ONE', '\t\tglow', '\t}']
    return '\n'.join(head + body + ['}', ''])


# name: albedo (rgb or callable), roughness, metallic
def _materials():
    m = {'floor': ((38, 40, 46), 0.06, 0.6), 'chrome': ((220, 222, 228), 0.03, 1.0),
         'ceiling': ((30, 30, 34), 0.7, 0.0), 'end_wall': ((70, 70, 76), 0.5, 0.0)}
    for _, _, name, rgb in ZONES:
        m['wall_' + name] = (tuple(int(c * 0.35) + 20 for c in rgb), 0.45, 0.0)
        m['strip_' + name] = (rgb, 0.3, 0.0)
    m['strip_white'] = ((255, 250, 240), 0.3, 0.0)
    return m


def shader_text():
    out = []
    for name in _materials():
        if name.startswith('strip_'):
            rgb = _materials()[name][0]
            out.append(material(name, STRIP_LIGHT, tuple(c / 255 for c in rgb)))
        else:
            out.append(material(name))
    return '\n'.join(out)


def write_textures(base):
    from PIL import Image, ImageDraw
    d = base / 'textures' / TEX
    d.mkdir(parents=True, exist_ok=True)
    Image.new('RGB', (64, 64), (128, 128, 255)).save(d / 'flat_n.tga')
    for name, (rgb, rough, metal) in _materials().items():
        im = Image.new('RGB', (256, 256), rgb)
        if name.startswith('wall_'):   # horizontal bands, so reflections of the walls are easy to read
            dr = ImageDraw.Draw(im)
            for y in range(0, 256, 64):
                dr.rectangle((0, y, 255, y + 6), fill=tuple(min(255, c + 60) for c in rgb))
        if name == 'floor':            # thin tile joints
            dr = ImageDraw.Draw(im)
            dr.rectangle((0, 0, 255, 1), fill=(20, 20, 24))
            dr.rectangle((0, 0, 1, 255), fill=(20, 20, 24))
        im.save(d / (name + '_d.tga'))
        # RMO: roughness, metallic, occlusion
        Image.new('RGB', (64, 64), (int(rough * 255), int(metal * 255), 255)).save(d / (name + '_rmo.tga'))


ARENA = '{\n\tmap\t\t"%s"\n\tlongname\t"xenjk Reflection Hall"\n\ttype\t\t"ffa duel"\n}\n' % NAME


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--out', default=str(ROOT / '.codex-research' / 'reflection_map' / 'build' / time.strftime('%Y%m%d-%H%M%S')))
    ap.add_argument('--fast', action='store_true', help='quick light pass')
    a = ap.parse_args(argv)
    out = Path(a.out).resolve()
    root = out / 'stage'
    base = root / 'base'
    for sub in ('maps', 'shaders', 'scripts'):
        (base / sub).mkdir(parents=True, exist_ok=True)
    mapfile = base / 'maps' / (NAME + '.map')
    mapfile.write_text(build_map(), encoding='utf-8', newline='\n')
    (base / 'shaders' / (NAME + '.shader')).write_text(shader_text(), encoding='utf-8', newline='\r\n')
    (base / 'scripts' / (NAME + '.arena')).write_text(ARENA, encoding='utf-8', newline='\r\n')
    write_textures(base)
    log = out / 'q3map2.log'
    print('building %s in %s' % (NAME, out))
    sm.q3map2(['-bsp', '-meta', '-patchmeta'], mapfile, root, log)
    sm.q3map2(['-vis', '-saveprt'], mapfile, root, log)
    light = ['-light', '-fast', '-filter', '-deluxe']
    light += ['-bounce', '1', '-samples', '2'] if a.fast else ['-bounce', '3', '-samples', '3', '-bouncegrid']
    sm.q3map2(light, mapfile, root, log)
    pk3 = out / ('zz_%s.pk3' % NAME)
    with zipfile.ZipFile(pk3, 'w', zipfile.ZIP_DEFLATED) as z:
        for p in sorted(base.rglob('*')):
            rel = p.relative_to(base).as_posix()
            if p.is_file() and not rel.endswith(('.prt', '.srf', '.map', '.bak')):
                z.write(p, rel)
    shutil.copy2(mapfile, out / mapfile.name)
    print('pk3: %s (%.1f MB)' % (pk3, pk3.stat().st_size / 1e6))
    return pk3


if __name__ == '__main__':
    main()
