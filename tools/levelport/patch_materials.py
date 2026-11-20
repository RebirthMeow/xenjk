"""patch_materials.py -- a kit whose material records a converter fix changed, patched without a scene rebuild.

  python -I patch_materials.py <base kit> <new kit name> --materials <scene build>/materials.json --template device_diode.mt
      [--template ...] [--area night_city_b] [--map cp_watson] [--renderer rd-vulkan_x86_64.dll] [--note "..."]

Each of the scene's materials made from a named template (materials.json's `base`, by file name) is converted again by
the current cp77mat from its mesh's Material.json, and its record in maps/<map>/materials.xscn (the copy the engine
serves: the last pack by name) rewritten where the fix changed only what the record holds: kind, flags, emissive colour
and intensity, tint, its colour image (a new one encoded into the patch pack, named as a rebuild names it), the image
paths emptied when the material now has none. A material whose new conversion needs more (a masked glow, a window, a
decal, a screen, a new cutout or blend) is listed, not patched: those wait for a rebuild,
where the converter fix lands anyway. First used for device_diode.mt (EmissiveColor1, 244 materials of Watson's
meshes, 126 in the scene: every corridor and bar lamp of that shader drew unlit; El Coyote's red pool-table lamp).

The new kit is build/<new kit name>/ beside the base kit: every file a hard link (engine, modules, packs), its settings,
launchers and KIT.txt copies, the renderer a copy when --renderer is given (as bin64/rd-vulkan_x86_64.dll), and one new pack,
home/base/zz_<map>_scene_patch.pk3, holding the patched materials.xscn stored (the renderer streams only from
stored files): it sorts after every scene pack (the engine's
FS_PathCmp upper-cases letters, so "_P" follows "_IMG", "2" and "3"), so it is the one served. A base kit that already
has a patch pack hands it on: its materials.xscn is the one patched, and the new pack replaces it. Never a kit made
before (a new name each time) and never installed. Converted levels are for local play only."""
import argparse
import ast
import collections
import json
import os
import re
import struct
import sys
import time
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import cp2gltf  # noqa: E402
import cp77mat  # noqa: E402

REC = 240                   # a material record: 3 x 64-byte image paths, then <2I10f


def served(hb, name):
    """(pack, member) the engine serves name from: the last pack by FS_PathCmp's order"""
    hit = None
    for pk in sorted(hb.glob('*.pk3'), key=lambda p: p.name.upper()):
        with zipfile.ZipFile(pk) as z:
            for n in z.namelist():
                if n.lower() == name.lower():
                    hit = (pk, n)
    return hit


def variant_key(m):
    """Whether a materials.json entry's key (cp2gltf.scene_part: base, name, record hash) carries a stand-in ad's
    variant or a signal lamp's zone after those three"""
    try:
        key = ast.literal_eval(m.get('mkey') or '()')
    except (ValueError, SyntaxError):
        return False
    return isinstance(key, tuple) and len(key) > 3


def record(sm, kind):
    """(kind, flags, 10 floats) as cp2gltf scene_part writes a plain material (no decal, no window)"""
    em = sm.get('emissive') or {}
    ecol = list(em.get('color', (1, 1, 1))) if em.get('tint', False) else [1.0, 1.0, 1.0]
    flags = ((1 if kind in ('cutout', 'blend') else 0) | (4 if sm.get('glow_mask') else 0) |
             (0x400 if sm.get('glow_albedo') else 0) | (0x800 if sm.get('leaves') else 0) | (0x200 if sm.get('vboard') else 0) |
             (0x1000 if sm.get('terrain') and kind == 'opaque' else 0))
    return cp2gltf.SCENE_KIND[kind], flags, ecol + [float(em.get('intensity', 0.0))] + [1.0, 1.0, 1.0, 1.0, 0.0, 0.0]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('base', type=Path, help='the kit to start from')
    ap.add_argument('name', help='the new kit: build/<name>')
    ap.add_argument('--materials', type=Path, required=True, help="the scene build's materials.json (the record order)")
    ap.add_argument('--template', action='append', default=[], help='a material template file name (repeatable)')
    ap.add_argument('--detail-cutout', action='store_true', help="with --template metal_base_det.mt: its tiled detail "
                    "baked in as a cutout (cp77mat.detail_cutout_image: chainlink fences, grates, "
                    "vents); an opaque one becomes a cutout")
    ap.add_argument('--placeholders', action='store_true', help="every multilayered material that came through on "
                    "CP2077's placeholder layer setup (cp77mat.placeholder_setups: the DEBUG texture), converted with "
                    "its siblings' setup: its colour, normal and ORM images and its tiled detail (maps/<map>.xdet)")
    ap.add_argument('--area', default='night_city_b')
    ap.add_argument('--map', default='cp_watson')
    ap.add_argument('--tiles-tag', default='11', help="the tiles tag the scene was built from (its ad_pool.json)")
    ap.add_argument('--renderer', type=Path, help='a renderer to copy into the kit (bin64)')
    ap.add_argument('--note', default='', help='a line for KIT.txt: what the patch is for')
    a = ap.parse_args()
    if not a.template and not a.placeholders:
        sys.exit('patch_materials: name a --template or --placeholders')
    if not re.fullmatch(r'[a-z0-9][a-z0-9_.-]*', a.name):
        sys.exit('patch_materials: a kit name is lower-case letters, digits, . _ -')
    base = a.base.resolve()
    out = base.parent / a.name
    if out.exists() or out == base:
        sys.exit('patch_materials: %s exists (a kit is made once; pick another name)' % out)
    hb = base / 'home' / 'base'
    xname = 'maps/%s/materials.xscn' % a.map
    src = served(hb, xname)
    if src is None:
        sys.exit('patch_materials: no %s in %s' % (xname, hb))
    with zipfile.ZipFile(src[0]) as z:
        blob = bytearray(z.read(src[1]))
    _, version, count, ofs = cp2gltf.xscn_head_of(bytes(blob))
    names = json.load(open(a.materials, encoding='utf-8'))
    if len(names) != count[5]:
        sys.exit('patch_materials: %s has %d materials, %s %d: not this scene' % (a.materials, len(names), xname, count[5]))

    work = cp2gltf.work_dir(a.area)
    # as cp2gltf's scene build makes them: the textures under meshes/, at its --scene-texsize, its DDS cache
    # (the multilayer bakes and tiled details at cp2gltf's defaults, which the scene builds use; its bake cache)
    mats = cp77mat.Materials(work / 'meshes', size=1024, bake_dir=work / 'bake', ml_size=512, detail_size=512,
                             detail_slots=8)
    mats.detail_cutout = a.detail_cutout
    pool = cp2gltf.tile_dir(work, a.tiles_tag) / 'ad_pool.json'    # (the stand-in ads the scene build drew from)
    if pool.exists():
        mats.ad_pool = [tuple(p) for p in json.load(open(pool, encoding='utf-8'))]
    images = cp2gltf.SceneImages(a.map, work / 'scene_dds')
    new_images = set()
    want = {t.lower() for t in a.template}
    stats = collections.Counter()
    unpatched, cache = [], {}
    details = {}    # material index -> its .xdet record's values (scene_material's detail list; None: no tiled detail)
    for m in names:
        # named: its base (materials.json: an .mi's path when it has one) or its own template (a material on an .mi
        # still names the template it is drawn with: fence_chainlink.mi's are metal_base_det.mt)
        named = m.get('base', '').replace('\\', '/').split('/')[-1].lower() in want
        mj = work / 'meshes' / (m['mesh'] + '.Material.json')
        if mj not in cache:
            cache[mj] = {}
            if mj.exists():
                d = json.load(open(mj, encoding='utf-8'))
                cp77mat.placeholder_setups(work / 'meshes', mj, d)
                cache[mj] = {r['Name']: r for r in d.get('Materials') or []}
        rec = cache[mj].get(m['name'])
        named = named or ((rec or {}).get('MaterialTemplate') or '').split(chr(92))[-1].lower() in want
        if not named and not (a.placeholders and (rec or {}).get('placeholder_setup')):
            continue
        stats['materials'] += 1
        if rec is None:
            unpatched.append((m['index'], m['mesh'], m['name'], 'no record in %s' % mj.name))
            continue
        cutout_det = (a.detail_cutout and (rec.get('MaterialTemplate') or '').split(chr(92))[-1].lower() == 'metal_base_det.mt'
                      and float((rec.get('Data') or {}).get('AlphaThreshold') or 0) > 0)
        if rec.get('placeholder_setup') or cutout_det:
            # a multilayered material, as the scene build converts it (cp2gltf.scene_material): its three images and
            # its tiled detail
            sm = cp2gltf.scene_material(rec, mats, images)
            kind = sm['kind']
            if ((kind != m['kind'] and not (cutout_det and kind == 'cutout')) or sm.get('decal') or sm.get('window')
                    or sm.get('glow_mask')):
                unpatched.append((m['index'], m['mesh'], m['name'], 'needs a rebuild (%s)' % kind))
                continue
            at = ofs[5] + REC * m['index']
            k, flags, vals = record(sm, kind)
            struct.pack_into('<2I10f', blob, at + 192, k, flags, *vals)
            paths = list((sm.get('paths') or [])[:3])
            paths += [''] * (3 - len(paths))
            for j, q in enumerate(paths):
                blob[at + 64 * j:at + 64 * j + 64] = q.encode('ascii').ljust(64, b'\0')
                if q:
                    new_images.add(q)
            details[m['index']] = sm.get('detail')
            for q in (sm.get('detail') or []):
                if isinstance(q, str) and q:
                    new_images.add(q)
            stats['patched'] += 1
            if cutout_det:
                stats['detail cutouts (%s -> %s)' % (m['kind'], kind)] += 1
            else:
                stats['given a setup' if (rec.get('Data') or {}).get('MultilayerSetup') else 'given none (flat grey)'] += 1
            continue
        if variant_key(m):
            # (a stand-in ad face's variant or a signal lamp's zone: the scene build converts the record with it,
            # mats.convert here would not)
            unpatched.append((m['index'], m['mesh'], m['name'], 'needs a rebuild (a variant or lamp zone)'))
            continue
        c = mats.convert(rec)
        kind, ex = c['kind'], c['extras']
        if kind == 'skip' or kind != m['kind'] and kind in ('cutout', 'blend') or ex.get('glow_mask') or ex.get('glow_albedo') \
                or ex.get('window') or ex.get('decal') or ex.get('screen') and not m.get('screen'):
            # (a screen keeps its maps/<map>.xscr record as built: only a material that was a screen already passes)
            unpatched.append((m['index'], m['mesh'], m['name'], 'needs a rebuild (%s, %s)' % (kind, sorted(
                k for k in ('glow_mask', 'glow_albedo', 'window', 'decal', 'screen') if ex.get(k)))))
            continue
        at = ofs[5] + REC * m['index']
        old = struct.unpack_from('<2I10f', blob, at + 192)
        sm = {'emissive': ex.get('lp_emissive'), 'leaves': ex.get('foliage_leaves'), 'vboard': ex.get('vector_board'),
              'terrain': ex.get('cp_template') == 'multilayered_terrain.mt'}
        k, flags, vals = record(sm, kind)
        flags |= old[1] & 0xF8          # (bits record() does not model: a signal lamp's 8 | lamp << 4, kept as built)
        # the colour image as scene_material names it (a hash of its pixels: the same image keeps its name); none: no
        # paths, as a rebuild writes them
        img = c.get('image')
        apath = images.add(img, 'a', alpha=kind in ('cutout', 'blend'), coverage=kind == 'cutout') if img is not None else ''
        oldpath = bytes(blob[at:at + 64]).split(b'\0', 1)[0].decode('ascii')
        if (k, flags) == old[:2] and all(abs(x - y) < 1e-6 for x, y in zip(vals, old[2:])) and apath == oldpath:
            stats['unchanged'] += 1
            continue
        struct.pack_into('<2I10f', blob, at + 192, k, flags, *vals)
        if img is None:
            blob[at:at + 192] = bytes(192)
        elif apath != oldpath:
            blob[at:at + 64] = apath.encode('ascii').ljust(64, b'\0')
            new_images.add(apath)
            stats['images'] += 1
        stats['patched'] += 1
        stats['%s -> %s' % (m['kind'], kind)] += 1

    # the tiled details (maps/<map>.xdet v2) of the materials patched from a placeholder: their slot images added to
    # the table, their records rewritten (cp2gltf.scene_map_files' layout)
    xdet_name, xdet = 'maps/%s.xdet' % a.map, None
    if details:
        xs = served(hb, xdet_name)
        if xs is None:
            sys.exit('patch_materials: no %s in %s' % (xdet_name, hb))
        with zipfile.ZipFile(xs[0]) as z:
            xb = z.read(xs[1])
        magic, ver, nmat, ntab = struct.unpack_from('<4s3I', xb, 0)
        if magic != b'XDET' or ver != 2 or nmat != count[5]:
            sys.exit('patch_materials: %s is not a v2 detail table of %d materials (%r %d %d)' % (
                xdet_name, count[5], magic, ver, nmat))
        recx = 8 * 20 + 128
        table = [xb[16 + 64 * i:16 + 64 * i + 64].split(b'\0', 1)[0].decode('ascii') for i in range(ntab)]
        tix = {t: i for i, t in enumerate(table)}
        base_r = 16 + 64 * ntab
        if len(xb) != base_r + recx * nmat:
            sys.exit('patch_materials: %s is %d bytes, its layout says %d' % (xdet_name, len(xb), base_r + recx * nmat))
        recs = [xb[base_r + recx * i:base_r + recx * (i + 1)] for i in range(nmat)]
        for mi, det in details.items():
            r = b''
            for sl in range(8):
                path, tile, nk, rr, cs = det[2 + 5 * sl:7 + 5 * sl] if det else ('', 0.0, 0.0, 0.0, 0.0)
                if path and path not in tix:
                    tix[path] = len(table)
                    table.append(path)
                r += struct.pack('<I4f', tix[path] if path else 0xFFFFFFFF, tile, nk, rr, cs)
            sh = (det[0], det[1]) if det else ('', '')
            recs[mi] = r + sh[0].encode('ascii').ljust(64, b'\0') + sh[1].encode('ascii').ljust(64, b'\0')
        xdet = b''.join([struct.pack('<4s3I', b'XDET', 2, nmat, len(table))]
                        + [t.encode('ascii').ljust(64, b'\0') for t in table] + recs)
        stats['detail table images added'] = len(table) - ntab

    # the patch pack, staged beside the new kit, then the kit by kit_derive (hard links to the base, its settings and
    # notes copied, the pack put in, KIT.json)
    import kit_derive
    t0 = time.time()
    pname = 'zz_%s_scene_patch.pk3' % a.map
    stage = out.with_name(out.name + '.pack')
    if stage.exists():
        sys.exit('patch_materials: %s is left from a stopped run: look at it, then move it away' % stage)
    stage.mkdir()
    # stored, not deflated: the renderer streams the scene's geometry only when every scene file is stored in its pack
    # (FS_RangeOpen; a deflated materials.xscn loaded the whole city's geometry, 4 GB more VRAM, and dropped the map)
    with zipfile.ZipFile(stage / pname, 'w', zipfile.ZIP_STORED) as z:
        z.writestr(xname, bytes(blob))
        if xdet is not None:
            z.writestr(xdet_name, xdet)
        images.write(z, sorted(new_images))
        if src[0].name.lower() == pname.lower():    # an earlier patch's images, still named by its records
            with zipfile.ZipFile(src[0]) as old_pk:
                for n in old_pk.namelist():
                    if n.lower() not in (xname.lower(), xdet_name.lower() if xdet is not None else '') \
                            and n not in new_images:
                        z.writestr(zipfile.ZipInfo(n), old_pk.read(n), compress_type=zipfile.ZIP_STORED)
    what = ', '.join(a.template + (['placeholder layer setups'] if a.placeholders else []))
    note = '\n'.join(x for x in ['%s (patch_materials.py, %s): %s with %s patched in %s (from %s): %s materials of %s, '
                                  '%d patched, %d unchanged, %d left for a rebuild.%s' % (
                                      a.name, time.strftime('%Y-%m-%d %H:%M'), base.name,
                                      xname + (' and ' + xdet_name if xdet is not None else ''), pname, src[0].name,
                                      stats['materials'], what, stats['patched'], stats['unchanged'], len(unpatched),
                                      ' Renderer: %s.' % a.renderer if a.renderer else ''), a.note] if x)
    r = kit_derive.derive(base, out, put={'home/base/' + pname: stage / pname}, renderer=a.renderer, note=note,
                          rem='%s: %s with its materials patched (%s, %s)' % (a.name, base.name, what, pname))
    os.unlink(stage / pname)                  # (the staging name only: the kit holds the pack, a hard link)
    stage.rmdir()
    nlink = r['links']
    print('%s: %d links in %.0f s; %s' % (out, nlink, time.time() - t0, dict(stats)))
    for u in unpatched:
        print('  not patched: %d %s %s: %s' % u)


if __name__ == '__main__':
    main()
