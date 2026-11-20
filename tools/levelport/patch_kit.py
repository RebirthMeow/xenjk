"""patch_kit.py -- a dev kit: an earlier kit with some tiles' collision chunks rebuilt.

  python -I patch_kit.py <base kit> <name> --builds DIR --manifest tiles/<tag>/chunks/manifest.json [--tiles a,b]

The new kit is build/cp_watson_dev-<name>/ beside the base kit: every file of the base kit as a hard link (the
engine, the game modules, the hub, the scene and chunk packs: nothing copied), its settings file as a copy, and one new
pack, home/base/zz_<map>_colchunks_patch.pk3. The engine reads a later-named pack first (files.cpp paksort and
FS_PathCmp: "colchunks_patch" sorts after "colchunks"), so the patch's maps/chunk_<tile>.bsp stand in for the base
pack's, and its maps/<map>.chunks and <map>_chunks.cfg (the base pack's lines, the rebuilt tiles' replaced) place them.
A rebuilt tile that came out empty (empty_chunk.txt) leaves both lists. A base kit that is itself a dev kit hands its
patched tiles on: they are read from its patch pack, and the new patch holds them and the new ones.

--builds: gltf2map --col-chunk output, <tile>/zz_chunk_<tile>.pk3 (cp2gltf.py chunks, then gltf2map per tile:
patch_tiles.py does both). --manifest: the chunks manifest those were compiled from; a tile's origin there must be
the base pack's (the same grid). Dev kits only: never a numbered kit (hub_col-N), and never installed. KIT.txt says
what is in it. --full-pack: the new kit's chunk pack is a whole one instead (home/base/zz_<map>_colchunks.pk3: the
base kit's chunks with these tiles' in their place; a patch pack holding many tiles' BSPs passes the engine's 4 GB
pack limit): members are copied as they are compressed (make_chunks_pk3.py's R2 raw copy), nothing inflated. Converted
levels are for local play only."""
import argparse
import hashlib
import json
import os
import re
import struct
import sys
import time
import zipfile
import zlib
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import colzbsp  # noqa: E402
import levelport as lp  # noqa: E402



READ = 4 << 20


PACK_MAX = 3_500_000_000      # bytes in one pack: under the engine's 4 GB (no zip64), with room for the directory


def member(pk3, name):
    """(zip info, offset of its compressed bytes) of one member of a pk3"""
    with zipfile.ZipFile(pk3) as z:
        i = z.getinfo(name)
    with open(pk3, 'rb') as f:
        f.seek(i.header_offset)
        h = f.read(30)
    if h[:4] != b'PK\x03\x04':
        sys.exit('%s: %s has no local header' % (pk3, name))
    nl, xl = struct.unpack_from('<HH', h, 26)
    return i, i.header_offset + 30 + nl + xl


def model0_bounds(pk3, name):
    """A BSP member's model 0 mins and maxs, its header and models lump out of a streaming inflate (make_chunks_pk3);
    an XBSZ member (stored: gltf2map --cook --xbsz, colzbsp) decoded whole"""
    i, start = member(pk3, name)
    if i.compress_type == zipfile.ZIP_STORED:
        with open(pk3, 'rb') as f:
            f.seek(start)
            magic = f.read(4)
        if colzbsp.is_xbsz(magic):
            with zipfile.ZipFile(pk3) as z:
                data = colzbsp.member_bsp(z, name)
            o = struct.unpack_from('<i', data, 8 + 8 * 7)[0]
            return np.array(struct.unpack_from('<3f', data, o)), np.array(struct.unpack_from('<3f', data, o + 12)), i
    if i.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
        sys.exit('%s: %s compression %d' % (pk3, name, i.compress_type))
    d = zlib.decompressobj(-15) if i.compress_type == zipfile.ZIP_DEFLATED else None
    head, want, keep, pos, left = b'', None, b'', 0, i.compress_size
    with open(pk3, 'rb') as f:
        f.seek(start)
        while left > 0 or (d is not None and d.unconsumed_tail):
            raw = f.read(min(READ, left)) if left > 0 else b''
            left -= len(raw)
            buf = d.decompress(d.unconsumed_tail + raw, READ) if d is not None else raw
            if len(head) < 8 + 8 * 8:
                head += buf[:8 + 8 * 8 - len(head)]
                if len(head) >= 8 + 8 * 8:
                    want = struct.unpack_from('<i', head, 8 + 8 * 7)[0]          # the models lump's offset
            if want is not None and pos + len(buf) > want and len(keep) < 24:
                keep += buf[max(0, want - pos):max(0, want - pos) + 24 - len(keep)]
            pos += len(buf)
            if want is not None and len(keep) >= 24:
                break
    if len(keep) < 24:
        sys.exit('%s: %s ended before its models lump' % (pk3, name))
    return np.array(struct.unpack_from('<3f', keep, 0)), np.array(struct.unpack_from('<3f', keep, 12)), i


def raw_copy(z, pk3, name):
    """One member's compressed bytes into the open (writing) ZipFile z as they are: its CRC and sizes kept"""
    i, start = member(pk3, name)
    zi = zipfile.ZipInfo(name, i.date_time)
    zi.compress_type, zi.CRC, zi.compress_size, zi.file_size = i.compress_type, i.CRC, i.compress_size, i.file_size
    zi.flag_bits = i.flag_bits & ~0x08                                       # sizes in the local header
    zi.external_attr, zi.create_system = i.external_attr, i.create_system
    zi.header_offset = z.fp.tell()
    z.fp.write(zi.FileHeader(zip64=False))
    with open(pk3, 'rb') as f:
        f.seek(start)
        left = i.compress_size
        while left > 0:
            b = f.read(min(READ, left))
            if not b:
                sys.exit('%s: %s is short' % (pk3, name))
            z.fp.write(b)
            left -= len(b)
    z.filelist.append(zi)
    z.NameToInfo[name] = zi
    z.start_dir = z.fp.tell()
    z._didModify = True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('base', type=Path, help='the kit to start from')
    ap.add_argument('name', help='the dev kit is build/cp_watson_dev-<name>')
    ap.add_argument('--builds', type=Path, required=True)
    ap.add_argument('--manifest', type=Path, required=True)
    ap.add_argument('--tiles', default='', help='comma-separated (default: every tile with a build in --builds)')
    ap.add_argument('--map', default='cp_watson')
    ap.add_argument('--note', default='', help='a line for KIT.txt: what the patch is for')
    ap.add_argument('--full-pack', action='store_true', help='a whole chunk pack, not a patch pack (see above)')
    a = ap.parse_args()
    if not re.fullmatch(r'[a-z0-9][a-z0-9_.-]*', a.name):
        sys.exit('patch_kit: a dev kit name is lower-case letters, digits, . _ -')
    base = a.base.resolve()
    out = base.parent / ('%s_dev-%s' % (a.map, a.name))
    if re.search(r'hub_col-\d+$', out.name) or out == base:
        sys.exit('patch_kit: %s is not a dev kit name' % out.name)
    if out.exists():
        sys.exit('patch_kit: %s exists (a dev kit is made once; pick another name)' % out)
    hb = base / 'home' / 'base'
    packs = sorted(hb.glob('zz_%s_colchunks*.pk3' % a.map), key=lambda p: p.name.upper())
    main_pack = hb / ('zz_%s_colchunks.pk3' % a.map)
    if main_pack not in packs:
        sys.exit('patch_kit: no %s in %s' % (main_pack.name, hb))

    # the base kit's chunk set, in the engine's order: a later pack's file over an earlier's
    serve = {}
    for pk in packs:
        with zipfile.ZipFile(pk) as z:
            for n in z.namelist():
                serve[n.lower()] = (pk, n)

    def read(name):
        pk, n = serve[name.lower()]
        with zipfile.ZipFile(pk) as z:
            return z.read(n)
    mname, cname = 'maps/%s.chunks' % a.map, '%s_chunks.cfg' % a.map
    head, lines = [], {}
    for line in read(mname).decode().splitlines():
        if line.startswith('//') or not line.strip():
            head.append(line)
        else:
            lines[line.split()[0].lower()] = line
    order = list(lines)
    inherited = {n for n, (pk, _) in serve.items() if pk != main_pack and n.startswith('maps/chunk_')}

    man = json.loads(a.manifest.read_text(encoding='utf-8'))
    origin = {c['tile']: lp.gltf_to_q3(np.array(c['offset_in_area_gltf_m'], np.float64)) for c in man['chunks']}
    tiles = [t for t in a.tiles.split(',') if t] or sorted(
        d.name for d in a.builds.iterdir() if (d / ('zz_chunk_%s.pk3' % d.name)).exists() or (d / 'empty_chunk.txt').exists())
    if not tiles:
        sys.exit('patch_kit: no tile builds in %s' % a.builds)
    new, report = {}, []
    for t in tiles:
        name = 'maps/chunk_%s.bsp' % t
        o = origin.get(t)
        if o is None:
            sys.exit('patch_kit: tile %s is not in %s' % (t, a.manifest))
        if np.any(np.abs(o - np.round(o)) > 1e-6):
            sys.exit('patch_kit: tile %s: origin %s is not whole units' % (t, o))
        o = np.round(o).astype(int)
        if name in lines and [int(v) for v in lines[name].split()[1:4]] != o.tolist():
            sys.exit('patch_kit: tile %s: origin %s here, %s in the base kit (another grid)' % (
                t, o.tolist(), lines[name].split()[1:4]))
        # a tile built again drops the base kit's extra BSPs of it (a patch's chunk_<tile>_p): the new build has them
        # or makes them needless (Watson tiles compiled whole over an earlier patch)
        for nm in [n for n in lines if n.startswith('maps/chunk_%s_' % t.lower())]:
            lines.pop(nm)
            new[nm] = None
            report.append('%s: the base kit\'s, dropped (tile %s built again)' % (nm, t))
        if (a.builds / t / 'empty_chunk.txt').exists():
            lines.pop(name, None)
            new[name] = None
            report.append('%s: empty (left out)' % t)
            continue
        src = a.builds / t / ('zz_chunk_%s.pk3' % t)
        with zipfile.ZipFile(src) as z:
            # the tile's chunk, and any more BSPs of the tile at its origin (patch_chunks.py: chunk_<tile>_p)
            names = [name] + sorted(n for n in z.namelist() if n.lower().startswith('maps/chunk_%s_' % t.lower())
                                    and n.lower().endswith('.bsp'))
        for nm in names:
            mins, maxs, i = model0_bounds(src, nm)
            new[nm] = (src, nm)
            if nm not in order:
                # (order, not lines: a base kit's chunk_<tile>_p dropped above left lines but stays in order, and a
                # rebuild carrying it again listed it twice, in the manifest and the cfg, and --full-pack packed it
                # twice)
                order.append(nm)
            lines[nm] = '%s %d %d %d %s %s' % (nm, *o, ' '.join('%.1f' % v for v in mins + o),
                                              ' '.join('%.1f' % v for v in maxs + o))
            report.append('%s: %s, %d bytes, crc32 %08x' % (nm, src, i.file_size, i.CRC))
    for name in sorted(inherited - set(new)):           # a dev base kit's own patched tiles, handed on
        if name in lines:
            new[name] = serve[name]
            report.append('%s: from the base kit\'s patch (%s)' % (name, serve[name][0].name))
    if a.full_pack:                                      # every other chunk of the base kit, as it is served
        for name in order:
            if name in lines and name not in new:
                new[name] = serve[name]

    # the chunk packs, staged beside the new kit, then the kit by kit_derive (hard links to the base kit, the base's
    # chunk packs this one replaces left out, its settings copied, KIT.json)
    import kit_derive
    t0 = time.time()
    stage = out.with_name(out.name + '.pack')
    if stage.exists():
        sys.exit('patch_kit: %s is left from a stopped run: look at it, then move it away' % stage)
    stage.mkdir()
    drop = ['home/base/' + f.name for f in packs if f != main_pack or a.full_pack]
    if a.full_pack:                                      # (the base kit's own copy of its pack: not this kit's)
        drop += [f.name for f in base.iterdir() if f.is_file() and f.name.lower().startswith('zz_%s_colchunks' % a.map)]
    pk = stage / ('zz_%s_colchunks%s.pk3' % (a.map, '' if a.full_pack else '_patch'))
    kept = [lines[n] for n in order if n in lines]
    cfg = ['// %s collision chunks: %s with %d rebuilt (patch_kit.py %s)' % (a.map, base.name, len(tiles), a.name)]
    cfg += ['cm_loadchunk %s %s %s %s' % tuple(l.split()[:4]) for l in kept]
    cfg.append('echo %s: %d collision chunks placed (dev kit %s)' % (a.map, len(kept), a.name))
    # packs of at most PACK_MAX bytes each (the engine reads no zip64: a pack past 4 GB is not read): the first holds
    # the manifest and the cfg, the rest are <pack>2, <pack>3, ... (chunk names are unique: their order does not matter)
    pks, z = [pk], zipfile.ZipFile(pk, 'w', zipfile.ZIP_DEFLATED)
    z.writestr(cname, '\n'.join(cfg) + '\n')
    z.writestr(mname, '\n'.join(head + ['// patched (dev kit %s): %s' % (a.name, ', '.join(tiles))] + kept) + '\n')
    for name in sorted(new) if not a.full_pack else [n for n in order if n in new]:
        if new[name] is None:
            continue
        if z.fp.tell() + member(*new[name])[0].compress_size + 4096 > PACK_MAX:
            z.close()
            pks.append(pk.with_name('%s%d.pk3' % (pk.stem, len(pks) + 1)))
            z = zipfile.ZipFile(pks[-1], 'w', zipfile.ZIP_DEFLATED)
        raw_copy(z, *new[name])
    z.close()
    shas = []
    for q in pks:
        h = hashlib.sha256()
        with open(q, 'rb') as f:
            for blk in iter(lambda: f.read(READ), b''):
                h.update(blk)
        shas.append('home/base/%s (sha256 %s)' % (q.name, h.hexdigest()))
    note = '\n'.join([
        '%s: a dev kit (patch_kit.py, %s). Not a numbered kit; not installed.' % (out.name, time.strftime('%Y-%m-%d %H:%M')),
        'Base: %s (every file a hard link, its settings a copy).' % base,
        'Patch: %s: the collision chunks of %s, from %s (manifest %s).' % (
            ', '.join(shas), ', '.join(tiles), a.builds, a.manifest),
        a.note,
        ''] + report)
    r = kit_derive.derive(base, out, put={'home/base/' + q.name: q for q in pks}, drop=drop, note=note,
                          kit_txt='replace', rem='DEV KIT %s: %s with the collision chunks of %s rebuilt (%s). Not a '
                          'numbered kit; not installed.' % (a.name, base.name, ', '.join(tiles), pk.name))
    for q in pks:
        os.unlink(q)                          # (the staging names only: the kit holds the packs, hard links)
    stage.rmdir()
    nlink = r['links']
    print('%s: %d links, %d chunks in %s (%d placed in all) in %.0f s' % (
        out, nlink, sum(1 for d in new.values() if d is not None), ', '.join(q.name for q in pks), len(kept),
        time.time() - t0))
    print('\n'.join(report))


if __name__ == '__main__':
    main()
