"""colmap.py -- a level port's gameplay map with its collision built without q3map2's compile.

gltf2map compiles a gameplay glb's collision with q3map2 (-bsp, in tiles past --tile-brushes). Here the collision is
cooked instead (gltf2map --col-chunk --cook --cook-python: colbrushes.py, byte-identical to q3map2's brushes), and
q3map2 only compiles a small hub and lights the result:

1. hub.glb: the glb with every collision node ignored but the playerclip ones (the boundary ring and lid: they also
   give the hub its extent, so the sky shell spans the map); gltf2map compiles it (-bsp, no light): entities, sky.
2. col.glb: the collision nodes but the playerclip ones; gltf2map --col-chunk --cook --cook-python: one chunk BSP.
3. colmerge.merge(hub, chunk): the hub's entities, sky surfaces and fogs, every brush, a tree of our own.
4. q3map2 -light on the merged BSP (the light grid characters are lit by sees every brush, as before); --light none
   skips it.
5. the pack: the hub's files with the lit BSP, plus the chunk's shader file.

    python tools/levelport/colmap.py <gameplay.glb> <map name> --out <build dir> [--light fast|full|none]
        [--jobs N] [-- <gltf2map arguments for the hub: --sky, --sun, --skylight, --worldspawn ...>]

$Q3MAP2 is the q3map2 for the hub and the light (gltf2map's). Steps' output: <out>/hub, <out>/col; the pack
<out>/zz_<map name>.pk3. Exit non-zero when a step fails.
"""
import argparse
import json
import shutil
import struct
import subprocess
import sys
import time
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import colmerge  # noqa: E402


def glb_doc(path):
    raw = Path(path).read_bytes()
    if raw[:4] != b'glTF':
        raise SystemExit('%s: not a .glb' % path)
    jlen = struct.unpack_from('<I', raw, 12)[0]
    return json.loads(raw[20:20 + jlen]), raw[20 + jlen:]


def write_glb(path, doc, rest):
    js = json.dumps(doc, separators=(',', ':')).encode('utf-8')
    js += b' ' * (-len(js) % 4)
    body = struct.pack('<II', len(js), 0x4E4F534A) + js + rest
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(b'glTF' + struct.pack('<II', 2, 12 + len(body)) + body)


def split_glb(src, hub, col):
    """hub: everything but the collision nodes that are not playerclip; col: the collision nodes that are not
    playerclip (entities, patches and render nodes ignored). Returns the node counts."""
    counts = {'hub_ignored': 0, 'col_ignored': 0, 'playerclip': 0, 'collision': 0}
    for which in ('hub', 'col'):
        doc, rest = glb_doc(src)
        for nd in doc.get('nodes', []):
            ex = nd.get('extras') if isinstance(nd.get('extras'), dict) else {}
            role = ex.get('lp')
            clip = role == 'collision' and 'playerclip' in (ex.get('contents') or [])
            if which == 'hub':
                drop = role == 'collision' and not clip
            else:
                drop = (role != 'collision' and (role in ('entity', 'patch') or 'mesh' in nd)) or clip
            if role == 'collision':
                counts['playerclip' if clip else 'collision'] += which == 'hub'
            if drop:
                nd['extras'] = dict(ex)
                nd['extras']['lp'] = 'ignore'
                counts[which + '_ignored'] += 1
        write_glb(hub if which == 'hub' else col, doc, rest)
    return counts


def run(cmd, log):
    t = time.time()
    with open(log, 'w', encoding='utf-8') as f:
        r = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)
    print('  %-6s %7.1f s  exit %d  (%s)' % (Path(log).stem, time.time() - t, r.returncode, log), flush=True)
    if r.returncode:
        sys.exit('colmap: %s failed, see %s' % (Path(log).stem, log))


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    extra = []
    if '--' in argv:
        k = argv.index('--')
        argv, extra = argv[:k], argv[k + 1:]
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('glb')
    ap.add_argument('name')
    ap.add_argument('--out', required=True)
    ap.add_argument('--light', default='fast', choices=('none', 'fast', 'full'))
    ap.add_argument('--jobs', type=int, default=0, help="gltf2map --jobs for the collision (0: gltf2map's default)")
    a = ap.parse_args(argv)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    hub_glb, col_glb = out / 'hub.glb', out / 'col.glb'
    counts = split_glb(a.glb, hub_glb, col_glb)
    print('colmap %s: %d collision nodes cooked, %d playerclip in the hub' % (a.name, counts['collision'],
                                                                           counts['playerclip']), flush=True)
    g2m = [sys.executable, '-u', str(HERE / 'gltf2map.py')]
    # 1. the hub: entities, sky, the ring (q3map2 -bsp on a small map), lit later
    run(g2m + [str(hub_glb), a.name, '--out', str(out / 'hub'), '--light', 'none', '--vis', 'none'] + extra,
        out / 'hub.log')
    # 2. the collision, cooked
    col_name = a.name + '_col'
    run(g2m + [str(col_glb), col_name, '--col-chunk', '--cook', '--cook-python', '--slab-cache', 'none',
               '--out', str(out / 'col')] + (['--jobs', str(a.jobs)] if a.jobs else []), out / 'col.log')
    with zipfile.ZipFile(out / 'col' / ('zz_%s.pk3' % col_name)) as z:
        chunk = z.read('maps/%s.bsp' % col_name)
        col_shaders = {n: z.read(n) for n in z.namelist() if n.startswith('shaders/')}
    chunk_bsp = out / 'col' / ('%s.bsp' % col_name)
    chunk_bsp.write_bytes(chunk)
    # 3. merged under one tree
    stage = out / 'hub' / 'stage'
    hub_bsp = stage / 'base' / 'maps' / ('%s.bsp' % a.name)
    hub_copy = out / 'hub' / ('%s.hub.bsp' % a.name)
    shutil.copy2(hub_bsp, hub_copy)
    t = time.time()
    rep = colmerge.merge(hub_bsp, [hub_copy, chunk_bsp])
    print('  merge  %7.1f s  %d brushes, %d leaves, leaf brushes median %g, max %d, %.1f MB' % (
        time.time() - t, rep['brushes'], rep['leafs'], rep['leaf_brushes_median'], rep['leaf_brushes_max'],
        rep['file_bytes'] / 1e6), flush=True)
    # 4. the light grid over every brush
    if a.light != 'none':
        import gltf2map
        light = ['-light', '-fast', '-filter', '-patchshadows']
        light += ['-bounce', '1', '-samples', '2'] if a.light == 'fast' else ['-bounce', '4', '-samples', '3',
                                                                              '-bouncegrid']
        gltf2map.q3map2(light, hub_bsp.with_suffix('.map'), stage, out / 'light.log')
    # 5. the pack: the hub's files, the lit BSP, the chunk's shaders
    pk3 = out / ('zz_%s.pk3' % a.name)
    tmp = pk3.with_name(pk3.name + '.tmp')
    with zipfile.ZipFile(out / 'hub' / ('zz_%s.pk3' % a.name)) as src, \
            zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
        for n in src.namelist():
            if n == 'maps/%s.bsp' % a.name:
                z.write(hub_bsp, n)
            else:
                z.writestr(src.getinfo(n), src.read(n))
        for n, data in col_shaders.items():
            z.writestr(n, data)
    with zipfile.ZipFile(tmp) as z:
        bad = z.testzip()
    if bad:
        sys.exit('colmap: %s does not read back (%s)' % (tmp, bad))
    tmp.replace(pk3)
    rep.update({'map': a.name, 'pk3': str(pk3), 'pk3_bytes': pk3.stat().st_size, 'seconds': round(time.time() - t0, 1),
                'nodes': counts, 'light': a.light})
    (out / 'colmap.json').write_text(json.dumps(rep, indent=1), encoding='utf-8')
    print('colmap -> %s (%.1f MB)  total %.0f s' % (pk3, pk3.stat().st_size / 1e6, time.time() - t0), flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
