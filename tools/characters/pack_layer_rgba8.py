"""pack_layer_rgba8.py -- the layer arrays that pack_layer_dds.py's BC7 proof left
on the RGBA8 path get uncompressed RGBA8 DDS layers, so they load as a read instead of a PNG decode and resample.

    python -I tools/characters/pack_layer_rgba8.py <in.pk3> <dump dir> <out.pk3> [--report r.json]

The layer files come from the renderer itself (char-fixes-vram, r_arrayDump 1): each array built from its images writes
each layer as it uploads it, the whole mip chain after its resample and the engine's mip filter, to
<home>/arraydump/<layer image>.dds (DX10 RGBA8). So the bytes uploaded from these files equal today's, by construction;
r_loadProfile 3 prints each array's uploaded-bytes hash on either path to check it.
For each dumped layer whose image is an entry of this pack, <image>.dds is added (stored) unless a DDS is already there
(that array keeps the RGBA8 path) or the image is read by anything but a layer line (the engine looks any name up as
<name>.dds first, so the file would replace it there too). Every other entry is copied byte for byte.
"""
import argparse
import json
import os
import re
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pack_layer_dds as ld  # noqa: E402
from packio import rewrite  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inp')
    ap.add_argument('dump')
    ap.add_argument('out')
    ap.add_argument('--report')
    a = ap.parse_args()
    dumps = {}
    for root, _, files in os.walk(os.path.join(a.dump, 'arraydump')):
        for f in files:
            if f.lower().endswith('.dds'):
                rel = os.path.relpath(os.path.join(root, f), os.path.join(a.dump, 'arraydump')).replace('\\', '/')
                dumps[rel.lower()] = os.path.join(root, f)
    z = zipfile.ZipFile(a.inp)
    names = {i.filename.lower(): i.filename for i in z.infolist()}
    other = set()
    for i in z.infolist():
        if i.filename.lower().endswith(('.shader', '.mtr', '.skin')):
            for line in re.sub(r'//[^\n]*', '', z.read(i).decode('latin-1')).splitlines():
                if not re.match(r'(?i)\s*layer\s', line):
                    other.update(ld.base_ref(r) for r in ld.REF.findall(line))
    new, skipped = {}, []
    for low, img in names.items():
        if not low.startswith('models/players/') or not low.endswith(('.png', '.tga', '.jpg', '.jpeg')):
            continue
        dname = os.path.splitext(img)[0] + '.dds'
        src = dumps.get(dname.lower())
        if not src:
            continue
        if dname.lower() in names:
            skipped.append({'image': img, 'why': 'a DDS is already beside it'})
        elif ld.base_ref(img) in other:
            skipped.append({'image': img, 'why': 'also read by another keyword or a skin'})
        else:
            data = open(src, 'rb').read()
            assert data[:4] == b'DDS ' and data[84:88] == b'DX10' and data[128] == 28, src
            new[dname] = data
    # always written: a batch's OUT is this run's copy even when nothing was added
    rewrite(z, a.out, add=sorted(new.items()))
    rep = {'in': a.inp, 'out': a.out, 'added': sorted(new), 'added_bytes': sum(len(d) for d in new.values()),
           'skipped': skipped}
    print('%s: %d RGBA8 layer DDS added (%.1f MB), %d skipped' % (os.path.basename(a.inp), len(new), rep['added_bytes'] / 2**20,
                                                               len(skipped)), flush=True)
    if a.report:
        with open(a.report, 'w', encoding='utf-8') as f:
            json.dump(rep, f, indent=1)


if __name__ == '__main__':
    main()
