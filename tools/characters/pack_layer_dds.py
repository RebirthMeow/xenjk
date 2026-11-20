"""pack_layer_dds.py -- BC7 DDS files beside a character pack's layer images.

    python -I tools/characters/pack_layer_dds.py <in.pk3> <out.pk3> [--report r.json] [--dry-run]

rd-vulkan builds a material's layer arrays (the `layer <albedoRough> <normalMetal> ...` keyword) from DDS files beside
the layer images when every layer of an array has one, all of one format, size and level count (vk_image.cpp
R_LoadArrayDDS); otherwise from the images as RGBA8, each resampled to the largest (at most 1024). This tool writes those
DDS files: for every stage whose layers' images are all present, each image gets <name>.dds at the array's size (the
engine's: the smallest power of two covering the largest layer, at most 1024; smaller layers resampled to it), BC7 with a full mip chain (the engine's 2x2 box filter), next to the image, which stays (the
renderer falls back to it, and other tools read it). Arrays the DDS files cannot serve are reported and left to the
RGBA8 path (an image in arrays of two sizes gets the larger; the other array keeps its images). Every other entry is copied byte for byte. New entries are stored (DDS ranges stream).
Proof: each DDS decodes within 30 dB of its image in every channel, alpha included, and has every level. An array with a
layer that fails gets no DDS files (the engine needs all or none) and stays on the RGBA8 path. Smaller layers are
resampled per channel (no premultiplied alpha). An image also read by another keyword or a skin is left alone: the engine
looks any name up as <name>.dds first, so the layer's DDS would replace it there too.
"""
import argparse
import io
import json
import os
import re
import struct
import sys
import zipfile

import numpy as np
from PIL import Image
import etcpak
import texture2ddecoder as t2d

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from packio import box_down, rewrite  # noqa: E402

LAYER = re.compile(r'(?im)^\s*layer\s+(\S+)\s+(\S+)')
MAX_SIZE = 1024
MIN_PSNR = 30.0


def dds_bc7(rgba):
    h, w = rgba.shape[:2]
    levels, cur = [], rgba
    while True:
        lh, lw = cur.shape[:2]
        pad = cur if lh >= 4 and lw >= 4 else np.pad(cur, ((0, max(0, 4 - lh)), (0, max(0, 4 - lw)), (0, 0)), mode='edge')
        levels.append(etcpak.compress_bc7(np.ascontiguousarray(pad).tobytes(), pad.shape[1], pad.shape[0]))  # RGBA in
        if lh == 1 and lw == 1:
            break
        cur = box_down(cur)
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000
    hdr = struct.pack('<4sIIIIIII44x', b'DDS ', 124, flags, h, w, len(levels[0]), 0, len(levels))
    pf = struct.pack('<II4sIIIII', 32, 0x4, b'DX10', 0, 0, 0, 0, 0)
    caps = struct.pack('<IIIII', 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    dx10 = struct.pack('<IIIII', 98, 3, 0, 1, 0)  # BC7_UNORM, TEXTURE2D, no misc, array size 1
    return hdr + pf + caps + dx10 + b''.join(levels), levels


def psnr(a, b):
    m = float(np.mean((a.astype(np.float32) - b.astype(np.float32)) ** 2))
    return 99.0 if m == 0 else 10 * np.log10(255.0 ** 2 / m)


REF = re.compile(r'(?i)models/[^\s",{}]+')


def base_ref(ref):
    r = ref.lower()
    return os.path.splitext(r)[0] if r.endswith(('.png', '.tga', '.jpg', '.jpeg', '.dds')) else r


def resize_straight(im, w, h):
    """per channel, no premultiplied alpha (PIL's RGBA resize multiplies colour by alpha, and a layer's alpha is its
    roughness), as the engine's ResampleTexture treats the four channels alike"""
    return np.stack([np.asarray(Image.fromarray(np.ascontiguousarray(im[:, :, k])).resize((w, h), Image.BILINEAR))
                     for k in range(4)], axis=-1)


def find(names, ref):
    """the pack entry an image reference names (any of the loaders' extensions), or None"""
    b = os.path.splitext(ref.lower())[0] if ref.lower().endswith(('.png', '.tga', '.jpg', '.jpeg', '.dds')) else ref.lower()
    for ext in ('.png', '.tga', '.jpg', '.jpeg'):
        if b + ext in names:
            return names[b + ext]
    return None


def convert(inp, out, dry):
    z = zipfile.ZipFile(inp)
    names = {i.filename.lower(): i.filename for i in z.infolist()}
    arrays, other = [], set()
    for i in z.infolist():
        low = i.filename.lower()
        if low.endswith(('.shader', '.mtr', '.skin')):
            text = re.sub(r'//[^\n]*', '', z.read(i).decode('latin-1'))
            # every image read by anything but a layer line (map, normalMap, a skin's surface): the engine looks any name
            # up as <name>.dds first, so a layer DDS beside such an image would replace it there too
            for line in text.splitlines():
                if not re.match(r'(?i)\s*layer\s', line):
                    other.update(base_ref(r) for r in REF.findall(line))
            if low.endswith('.skin'):
                continue
            for st in re.findall(r'\{([^{}]*)\}', text):
                lines = LAYER.findall(st)
                if lines:
                    arrays.append([a for a, _ in lines])
                    normals = [nrm for _, nrm in lines if nrm != '-']
                    if normals:   # a stage whose layers have no normal maps has no normal array
                        arrays.append(normals)
    cache, new, recs, fails, skipped = {}, {}, [], [], []

    def img(e):
        if e not in cache:
            cache[e] = np.asarray(Image.open(io.BytesIO(z.read(e))).convert('RGBA'))
        return cache[e]

    def target(ims):
        """the engine's array size (R_LoadArrayLayers): the smallest power of two covering the largest layer, at most 1024"""
        out = []
        for k in (1, 0):
            m = max(im.shape[k] for im in ims)
            t = 1
            while t < m and t < MAX_SIZE:
                t <<= 1
            out.append(t)
        return out[0], out[1]  # w, h

    plans = []
    want = {}   # image entry: the largest array size it is in (one DDS per image)
    for arr in arrays:
        ents = [find(names, r) for r in arr]
        if not arr or any(e is None for e in ents):
            skipped.append({'array': arr, 'why': 'a layer image is not in this pack'})
            continue
        if any(base_ref(e) in other for e in ents):
            skipped.append({'array': arr, 'why': 'an image is also read by another keyword or a skin'})
            continue
        have = [(os.path.splitext(e)[0] + '.dds').lower() in names for e in ents]
        if all(have):
            continue    # every layer has its DDS already
        if any(have):   # R_LoadArrayDDS needs every layer alike: new files for the others would never be read
            skipped.append({'array': arr, 'why': 'some layers already have a DDS'})
            continue
        tw, th = target([img(e) for e in ents])
        if tw < 4 or th < 4:
            skipped.append({'array': arr, 'why': 'smaller than a block'})
            continue
        plans.append((arr, ents, (tw, th)))
        for e in ents:
            if e not in want or want[e][0] * want[e][1] < tw * th:
                want[e] = (tw, th)
    for arr, ents, size in plans:
        if any(want[e] != size for e in ents):
            skipped.append({'array': arr, 'why': 'an image is also in a larger array'})
            continue
        for e in ents:
            dname = os.path.splitext(e)[0] + '.dds'
            if dname.lower() in names or dname in new or e in fails:
                continue
            im = img(e)
            src_h, src_w = im.shape[:2]
            tw, th = size
            if (src_w, src_h) != (tw, th):	# resampled to the array's size, as the engine does with the images
                im = resize_straight(im, tw, th)
            data, levels = dds_bc7(im)
            h, w = im.shape[:2]
            dec = np.frombuffer(t2d.decode_bc7(levels[0], w, h), np.uint8).reshape(h, w, 4)[:, :, [2, 1, 0, 3]]
            ch = [round(psnr(im[:, :, k], dec[:, :, k]), 2) for k in range(4)]
            ok = bool(min(ch) >= MIN_PSNR and len(levels) == int(np.log2(max(w, h))) + 1)
            recs.append({'image': e, 'dds': dname, 'w': w, 'h': h, 'source': [src_w, src_h], 'psnr_rgba': ch,
                         'image_bytes': z.getinfo(e).file_size, 'dds_bytes': len(data), 'ok': ok})
            if not ok:
                fails.append(e)
                continue
            new[dname] = data
    # an array is served by DDS files only when every layer has one: drop the files of arrays with a failed layer, unless
    # a whole array that passed needs the same file
    keep = set()
    for arr, ents, size in plans:
        if all(want[e] == size for e in ents):
            if any(e in fails for e in ents):
                skipped.append({'array': arr, 'why': 'a layer failed the proof: the array stays RGBA8'})
            else:
                keep.update(os.path.splitext(e)[0] + '.dds' for e in ents)
    new = {d: v for d, v in new.items() if d in keep}
    recs = [dict(r, written=r['dds'] in new) for r in recs]
    if not dry:   # always written: a batch's OUT is this run's copy even when nothing changed
        rewrite(z, out, add=new.items())
    # in VRAM (full chains): the RGBA8 arrays these images fill vs BC7
    rgba = sum(r['w'] * r['h'] * 4 * 4 // 3 for r in recs if r['written'])
    bc7 = sum(r['dds_bytes'] - 148 for r in recs if r['written'])
    rep = {'in': inp, 'out': out if not dry else None, 'dds_written': len(new), 'arrays': len(arrays),
           'skipped': skipped, 'proof_failures': fails, 'vram_rgba8_bytes': rgba, 'vram_bc7_bytes': bc7, 'images': recs}
    print('%s: %d layer images to BC7 DDS (%d arrays, %d skipped), layer VRAM %.1f -> %.1f MB, %d proof failures' % (
        os.path.basename(inp), len(new), len(arrays), len(skipped), rgba / 2**20, bc7 / 2**20, len(fails)), flush=True)
    return rep


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inp')
    ap.add_argument('out')
    ap.add_argument('--report')
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    rep = convert(a.inp, a.out, a.dry_run)
    if a.report:
        with open(a.report, 'w', encoding='utf-8') as f:
            json.dump(rep, f, indent=1)
    sys.exit(0)   # a failed layer leaves its array on the RGBA8 path (reported), not a broken pack


if __name__ == '__main__':
    main()
