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
"""cp77_ads_fetch.py -- the ads folder cp77_ads.py reads (CP77_ADS_DIR: ads.json and rendered/), rebuilt from the
installed CP2077: its advertisement records from TweakDB and each ad's library items composited offline to images.

Reads the game (CP77_GAME): r6/cache/tweakdb.bin and the archive/pc/content and archive/pc/ep1 archives.
Writes into <ads dir>, one step each:
  dump    ads.json: every advertisement group and ad record with its fields and definitions (format -> library item
          of the ad's .inkwidget), read by cp77_ads_dump.ps1 through WolvenKit's assemblies in PowerShell 7
  fetch   raw/   base\\gameplay\\gui\\world\\adverts\\ .inkwidget, .inkatlas and .xbm files, unbundled;
          json/  those widgets, atlases and textures serialised (`convert serialize`), flat by file name (the
                 header's ArchiveFileName names the depot path; a same-named file of another folder is refused);
          png/   the adverts textures uncooked to png (uncook also leaves the cooked .xbm beside each);
          fetch_ads.log  WolvenKit's output
  render  rendered/<LibraryName>.png (or <LibraryName>__<resource>.png when the item's name is also drawn from
          another resource): each definition of each ad in ads.json composited from json/ and png/;
          rendered/manifest.json, rendered/sheet.jpg (a contact sheet of 24 renders)
Only the adverts folder is fetched: ads whose widget lives elsewhere (vending machine ads, quest billboards) are
listed in the manifest's _failures as "inkwidget not extracted".

  python -I cp77_ads_fetch.py <ads dir> [--steps dump,fetch,render] [--jobs N] [--only TEXT] [--sheet none]
  python -I cp77_ads_fetch.py <ads dir> --one <inkwidget json> <library item> <out.png>

Environment (paths.py): CP77_GAME, WOLVENKIT_CLI and WOLVENKIT_DOTNET (dump and fetch; the dump loads the DLLs
beside WolvenKit.CLI.exe), PWSH (dump). Use WolvenKit 10.0.0-beta.1 (the published ads folder's build): 9.0.1's
name and path tables predate the game's current patch, so its dump leaves the newer ads as hashes (0x...) and its
fetch misses 20 adverts files. Never run WolvenKit while the game runs. The render step drops to idle CPU priority (--normal-priority keeps it).

Manifest (rendered/manifest.json): {ad: {Format: png}} for each ad's first definition of a format, plus
_variants {ad: {Format: [png, ...]}} when an ad has several definitions of one format, _failures
{ad: {"Format/LibraryName": reason}}, _notes {ad: {"Format/LibraryName": [...]}}, _static_equivalents
{animated png: png of the same LibraryName from the static resource}, _summary. Paths are relative to the ads folder.

The compositor (render(), importable) follows these ink conventions:
  * Draw order: canvas childOrder "Backward" (98% of canvases) draws the LAST child first, so the first
    child is on top; "Forward" draws first-to-last.  Backgrounds sit last in Backward lists.
  * Canvas children are placed by layout.anchor only; HAlign/VAlign are ignored inside canvases
    (they only matter in panels).  Per axis: Left -> margin.left, Right -> parent - margin.right,
    Center -> parent/2 + (margin.left - margin.right), Fill -> [margin.left, parent - margin.right]
    (size taken from the parent).  The widget's anchorPoint (pivot, in its own size) sits on that point.
  * Size: fitToContent images use the native pixel size of the atlas part in the 4K slot (slot 0;
    1080p/720p slots scale x2/x3 when slot 0 is absent); other widgets use layout size.
  * renderTransform (translation, rotation clockwise in degrees, shear, scale) is applied about
    renderTransformPivot (fraction of the widget size) after layout and is inherited by children.
  * tintColor RGB and alpha*opacity multiply down the hierarchy; invisible widgets are skipped.
  * inkMaskWidget: the coverage (part alpha; engine white_mask_atlas = solid) of all mask widgets in a
    canvas multiplies the composite of that canvas's other children (mask opacity is ignored: designers
    set it to 0.01 to hide the mask itself).  invertMask inverts.
  * Effects: inkLinearWipeEffect / glitch / light sweep / censorship / blur are animation or settings
    driven and are ignored (static state); inkColorFillEffect is ignored.
  * inkGradientWidget: angle 0 runs startColor (top edge) to endColor (bottom edge).
  * inkVideoWidget: no video frames are available offline -> transparent, recorded as a note.
  * inkTextWidget: literal text drawn with a substitute Windows font (%WINDIR%\\Fonts: bahnschrift, arial; PIL's
    default font without them); LocKey text is skipped (noted).
  * Static state of animated ads (inkanim files are not available), all recorded in the notes:
      - widgets with an inkCensorshipController (censor bars; censorship is off by default) are hidden;
      - an opaque solid rectangle covering >=90% of the frame above drawn content is a flash/fade
        transition and is skipped;
      - stacked copies of one image part at one layout keep the copy tinted closest to white; stacked
        text widgets keep the topmost; in animated resources, same-layout sibling canvases holding
        different art are slides and only the bottom (first drawn) one is kept.
  * Output is straight-alpha RGBA.  Uncovered areas stay transparent (a handful of ads, e.g. Moonchies,
    leave part of the frame to the screen behind them); composite over black for an emissive screen.
Converted levels are for local play only."""
import argparse
import json
import math
import os
import subprocess
import sys
import time
from collections import OrderedDict
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import paths as lpaths  # noqa: E402

BS = chr(92)


# ----------------------------------------------------------------------------------------------- helpers
def cv(v):
    return v.get('$value') if isinstance(v, dict) and '$value' in v else v


def depot(ref):
    if not isinstance(ref, dict):
        return None
    p = cv(ref.get('DepotPath'))
    if p in (None, '0', 0):
        return None
    return str(p)


def wdata(w):
    if isinstance(w, dict) and 'Data' in w and isinstance(w['Data'], dict):
        return w['Data']
    return w


def children(w):
    k = w.get('children')
    if isinstance(k, dict):
        k = (wdata(k) or {}).get('children')
    return [wdata(c) for c in (k or []) if isinstance(c, dict) and wdata(c)]


def vec(d, a=0.0, b=0.0):
    d = d or {}
    return float(d.get('X', a)), float(d.get('Y', b))


def color(c, default=(1.0, 1.0, 1.0, 1.0)):
    if not isinstance(c, dict):
        return default
    return (float(c.get('Red', 1)), float(c.get('Green', 1)), float(c.get('Blue', 1)), float(c.get('Alpha', 1)))


class Ctx:
    def __init__(self, root):
        self.root = root
        self.jdir = os.path.join(root, 'json')
        self.pdir = os.path.join(root, 'png')
        self._json = OrderedDict()
        self._tex = OrderedDict()
        self._atlas = {}
        self.notes = []

    # json by resource file name (flat folder)
    def load_json(self, depot_path, limit=4):
        name = depot_path.replace('/', BS).split(BS)[-1] + '.json'
        if name in self._json:
            self._json.move_to_end(name)
            return self._json[name]
        p = os.path.join(self.jdir, name)
        if not os.path.exists(p):
            return None
        with open(p, encoding='utf-8') as f:
            d = json.load(f)
        # the json folder is flat: make sure this file really is the requested depot path
        src = str((d.get('Header') or {}).get('ArchiveFileName') or '').replace('/', BS).lower()
        want = depot_path.replace('/', BS).lower()
        if src and not src.endswith(BS + want) and src != want:
            d = None
        self._json[name] = d
        while len(self._json) > limit:
            self._json.popitem(last=False)
        return d

    def texture(self, xbm_depot):
        """uint8 HxWx4 straight-alpha texture for an .xbm depot path, or None."""
        if xbm_depot in self._tex:
            self._tex.move_to_end(xbm_depot)
            return self._tex[xbm_depot]
        rel = xbm_depot.replace(BS, os.sep)
        if rel.lower().endswith('.xbm'):
            rel = rel[:-4]
        p = os.path.join(self.pdir, rel + '.png')
        arr = None
        if os.path.exists(p):
            im = Image.open(p)
            im.load()
            arr = np.asarray(im.convert('RGBA'))
        self._tex[xbm_depot] = arr
        while len(self._tex) > 6:
            self._tex.popitem(last=False)
        return arr

    def atlas(self, atlas_depot):
        if atlas_depot in self._atlas:
            return self._atlas[atlas_depot]
        d = self.load_json(atlas_depot, limit=8)
        a = None
        if d is not None:
            a = Atlas(self, d['Data']['RootChunk'])
        self._atlas[atlas_depot] = a
        return a


class Atlas:
    # slot index -> resolution factor relative to the 4K slot
    SLOT_SCALE = (1.0, 2.0, 3.0)

    def __init__(self, ctx, rc):
        self.ctx = ctx
        self.slots = []  # [(texture depot, {part: (l,t,r,b) uv})]
        for s in (rc.get('slots') or {}).get('Elements') or []:
            parts = {}
            for p in s.get('parts') or []:
                u = p.get('clippingRectInUVCoords') or {}
                parts[str(cv(p.get('partName')))] = (float(u.get('Left', 0)), float(u.get('Top', 0)),
                                                     float(u.get('Right', 1)), float(u.get('Bottom', 1)))
            self.slots.append((depot(s.get('texture')), parts))
        if rc.get('parts'):
            parts = {}
            for p in rc['parts']:
                u = p.get('clippingRectInUVCoords') or {}
                parts[str(cv(p.get('partName')))] = (float(u.get('Left', 0)), float(u.get('Top', 0)),
                                                     float(u.get('Right', 1)), float(u.get('Bottom', 1)))
            self.slots.insert(0, (depot(rc.get('texture')), parts))

    def part(self, name):
        """-> (texture uint8 array, uv rect, native size in 4K units) or reason string."""
        name = str(name)
        why = 'part %r not in atlas' % name
        for i, (tex, parts) in enumerate(self.slots):
            if not tex:
                continue
            uv = parts.get(name)
            if uv is None and name in ('None', '') and parts:
                uv = (0.0, 0.0, 1.0, 1.0)
            if uv is None:
                continue
            arr = self.ctx.texture(tex)
            if arr is None:
                why = 'texture png missing for %s' % tex.split(BS)[-1]
                continue
            h, w = arr.shape[:2]
            k = self.SLOT_SCALE[i] if i < len(self.SLOT_SCALE) else 1.0
            nat = ((uv[2] - uv[0]) * w * k, (uv[3] - uv[1]) * h * k)
            return arr, uv, nat
        return why


# ----------------------------------------------------------------------------------------------- affine
def mat(a, b, c, d, e, f):
    return np.array([[a, b, c], [d, e, f], [0, 0, 1]], dtype=np.float64)


def T(x, y):
    return mat(1, 0, x, 0, 1, y)


def local_transform(w, size):
    rt = w.get('renderTransform') or {}
    sx, sy = vec(rt.get('scale'), 1, 1)
    hx, hy = vec(rt.get('shear'))
    tx, ty = vec(rt.get('translation'))
    rot = math.radians(float(rt.get('rotation') or 0))
    px, py = vec(w.get('renderTransformPivot'), 0.5, 0.5)
    px *= size[0]
    py *= size[1]
    c, s = math.cos(rot), math.sin(rot)
    R = mat(c, -s, 0, s, c, 0)
    Sh = mat(1, hx, 0, hy, 1, 0)
    S = mat(sx, 0, 0, 0, sy, 0)
    return T(tx, ty) @ T(px, py) @ R @ Sh @ S @ T(-px, -py)


ANCH = {
    'TopLeft': ('L', 'T'), 'TopCenter': ('C', 'T'), 'TopRight': ('R', 'T'),
    'CenterLeft': ('L', 'C'), 'Centered': ('C', 'C'), 'CenterRight': ('R', 'C'),
    'BottomLeft': ('L', 'B'), 'BottomCenter': ('C', 'B'), 'BottomRight': ('R', 'B'),
    'TopFillHorizontaly': ('F', 'T'), 'CenterFillHorizontaly': ('F', 'C'), 'BottomFillHorizontaly': ('F', 'B'),
    'LeftFillVerticaly': ('L', 'F'), 'CenterFillVerticaly': ('C', 'F'), 'RightFillVerticaly': ('R', 'F'),
    'Fill': ('F', 'F'),
}

CENTER_MARGIN_K = 1.0  # Centered axis offset = k*(margin.left - margin.right)


def axis_place(mode, lo, hi, extent, size, ap):
    """lo/hi = near/far margins; returns (start, size)."""
    if mode == 'F':
        size = max(0.0, extent - lo - hi)
        return lo, size
    if mode in ('L', 'T'):
        p = lo
    elif mode in ('R', 'B'):
        p = extent - hi
    else:
        p = extent / 2.0 + CENTER_MARGIN_K * (lo - hi)
    return p - ap * size, size


# ----------------------------------------------------------------------------------------------- raster
class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.buf = np.zeros((h, w, 4), np.float32)  # premultiplied


def _grid(M, lw, lh, W, H):
    """Inverse-map the output bbox of local rect [0,lw]x[0,lh] under M. -> (y0,x0, lx, ly, edge aa) or None"""
    if lw <= 0 or lh <= 0:
        return None
    pts = M @ np.array([[0, lw, lw, 0], [0, 0, lh, lh], [1, 1, 1, 1]], dtype=np.float64)
    x0 = max(0, int(math.floor(pts[0].min())))
    x1 = min(W, int(math.ceil(pts[0].max())))
    y0 = max(0, int(math.floor(pts[1].min())))
    y1 = min(H, int(math.ceil(pts[1].max())))
    if x1 <= x0 or y1 <= y0:
        return None
    try:
        Mi = np.linalg.inv(M)
    except np.linalg.LinAlgError:
        return None
    xs = np.arange(x0, x1, dtype=np.float32) + 0.5
    ys = np.arange(y0, y1, dtype=np.float32) + 0.5
    X, Y = np.meshgrid(xs, ys)
    lx = (Mi[0, 0] * X + Mi[0, 1] * Y + Mi[0, 2]).astype(np.float32)
    ly = (Mi[1, 0] * X + Mi[1, 1] * Y + Mi[1, 2]).astype(np.float32)
    # pixels per local unit along each local axis (for edge antialiasing)
    ppx = float(math.hypot(M[0, 0], M[1, 0])) or 1.0
    ppy = float(math.hypot(M[0, 1], M[1, 1])) or 1.0
    ex = np.clip(np.minimum(lx, lw - lx) * ppx + 0.5, 0, 1)
    ey = np.clip(np.minimum(ly, lh - ly) * ppy + 0.5, 0, 1)
    cov = ex * ey
    return y0, y1, x0, x1, lx, ly, cov, ppx, ppy


def _bilinear(img, tx, ty):
    """img float32 HxWxC (premultiplied); tx,ty texel coords (centers at +0.5)."""
    h, w = img.shape[:2]
    tx = np.clip(tx - 0.5, 0, w - 1)
    ty = np.clip(ty - 0.5, 0, h - 1)
    x0 = np.floor(tx).astype(np.int32)
    y0 = np.floor(ty).astype(np.int32)
    x1 = np.minimum(x0 + 1, w - 1)
    y1 = np.minimum(y0 + 1, h - 1)
    fx = (tx - x0)[..., None]
    fy = (ty - y0)[..., None]
    a = img[y0, x0] * (1 - fx) + img[y0, x1] * fx
    b = img[y1, x0] * (1 - fx) + img[y1, x1] * fx
    return a * (1 - fy) + b * fy


def _prep_part(arr, uv, out_w, out_h):
    """crop the uv rect from the texture; downsample (PIL) when strongly minified. -> premult float32"""
    h, w = arr.shape[:2]
    l = max(0, int(math.floor(uv[0] * w)))
    t = max(0, int(math.floor(uv[1] * h)))
    r = min(w, max(l + 1, int(math.ceil(uv[2] * w))))
    b = min(h, max(t + 1, int(math.ceil(uv[3] * h))))
    crop = arr[t:b, l:r]
    cw, ch = crop.shape[1], crop.shape[0]
    tw, th = max(1, int(round(out_w))), max(1, int(round(out_h)))
    if tw * 1.5 < cw or th * 1.5 < ch:
        f = np.asarray(crop, np.float32) / 255.0
        f[..., :3] *= f[..., 3:4]
        nw, nh = min(cw, max(tw, 1)), min(ch, max(th, 1))
        chans = [np.asarray(Image.fromarray(f[..., i]).resize((nw, nh), Image.LANCZOS)) for i in range(4)]
        img = np.clip(np.stack(chans, -1), 0, 1).astype(np.float32)
    else:
        img = np.asarray(crop, np.float32) / 255.0
        img[..., :3] *= img[..., 3:4]
    # fractional crop offsets so the uv rect maps exactly onto the cropped pixels
    sub = ((uv[0] * w - l) / cw, (uv[1] * h - t) / ch, (uv[2] * w - l) / cw, (uv[3] * h - t) / ch)
    return img, sub


def composite(dst, y0, y1, x0, x1, src):
    """premultiplied over"""
    d = dst[y0:y1, x0:x1]
    d *= (1.0 - src[..., 3:4])
    d += src


# ----------------------------------------------------------------------------------------------- renderer
class Renderer:
    def __init__(self, ctx, animated=False):
        self.ctx = ctx
        self.notes = []
        self.animated = animated
        self.images_drawn = 0

    def note(self, s):
        if s not in self.notes:
            self.notes.append(s)

    # native size for fitToContent widgets
    def native_size(self, w):
        t = w.get('$type')
        if t in ('inkImageWidget', 'inkMaskWidget'):
            ap = depot(w.get('textureAtlas'))
            if ap:
                a = self.ctx.atlas(ap)
                if a is not None:
                    r = a.part(cv(w.get('texturePart')))
                    if not isinstance(r, str):
                        return r[2]
        if t == 'inkTextWidget':
            tx = self.text_of(w)
            if tx:
                im = self.text_image(w, tx)
                if im is not None:
                    return im.shape[1], im.shape[0]
        if t == 'inkHorizontalPanelWidget':
            return self.panel_layout(w)[0]
        return vec(w.get('size'))

    def size_of(self, w):
        if w.get('fitToContent') and w.get('$type') in ('inkImageWidget', 'inkMaskWidget', 'inkTextWidget',
                                                         'inkHorizontalPanelWidget'):
            return self.native_size(w)
        return vec(w.get('size'))

    def layout_in(self, w, pw, ph):
        """-> (x, y, w, h) in parent's local units"""
        L = w.get('layout') or {}
        m = L.get('margin') or {}
        ml, mt, mr, mb = (float(m.get(k, 0) or 0) for k in ('left', 'top', 'right', 'bottom'))
        ax, ay = vec(L.get('anchorPoint'))
        sw, sh = self.size_of(w)
        hm, vm = ANCH.get(L.get('anchor'), ('L', 'T'))
        x, sw = axis_place(hm, ml, mr, pw, sw, ax)
        y, sh = axis_place(vm, mt, mb, ph, sh, ay)
        return x, y, sw, sh

    def panel_layout(self, w):
        """horizontal panel: children left to right. -> ((w,h), [(child, x, y, cw, ch)])"""
        cm = w.get('childMargin') or {}
        kids = [c for c in children(w) if c.get('visible', 1) or c.get('affectsLayoutWhenHidden')]
        if w.get('childOrder') == 'Backward':
            kids = kids[::-1]
        items = []
        x = 0.0
        hmax = 0.0
        for c in kids:
            L = c.get('layout') or {}
            m = L.get('margin') or {}
            cw, ch = self.size_of(c)
            x += float(m.get('left', 0) or 0) + float(cm.get('left', 0) or 0)
            items.append([c, x, float(m.get('top', 0) or 0), cw, ch, L.get('VAlign')])
            x += cw + float(m.get('right', 0) or 0) + float(cm.get('right', 0) or 0)
            hmax = max(hmax, ch + float(m.get('top', 0) or 0) + float(m.get('bottom', 0) or 0))
        size = (x, hmax) if w.get('fitToContent') else vec(w.get('size'))
        for it in items:
            if it[5] == 'Center':
                it[2] += (size[1] - it[4]) / 2.0
            elif it[5] == 'Bottom':
                it[2] = size[1] - it[4]
            elif it[5] == 'Fill':
                it[4] = size[1]
        return size, [tuple(i[:5]) for i in items]

    # --- drawing primitives ------------------------------------------------------------------------
    def draw_quad(self, cv_, M, lw, lh, sampler, tint):
        g = _grid(M, lw, lh, cv_.w, cv_.h)
        if g is None:
            return
        y0, y1, x0, x1, lx, ly, cov, ppx, ppy = g
        col = sampler(lx / lw, ly / lh, lx, ly, ppx, ppy)  # premult rgba, shape (h,w,4)
        if col is None:
            return
        tr, tg, tb, ta = tint
        col = col * np.array([tr * ta, tg * ta, tb * ta, ta], np.float32)
        col *= cov[..., None]
        composite(cv_.buf, y0, y1, x0, x1, col)

    def tex_sampler(self, w, arr, uv, nat, lw, lh, M):
        ppx = float(math.hypot(M[0, 0], M[1, 0]))
        ppy = float(math.hypot(M[0, 1], M[1, 1]))
        tile = w.get('tileType') or 'NoTile'
        tilex = tile in ('Both', 'Horizontal')
        tiley = tile in ('Both', 'Vertical')
        nw, nh = max(1.0, nat[0]), max(1.0, nat[1])
        out_w = (nw if tilex else lw) * ppx
        out_h = (nh if tiley else lh) * ppy
        img, sub = _prep_part(arr, uv, out_w, out_h)
        mir = w.get('mirrorType') or 'NoMirror'
        ih, iw = img.shape[:2]

        def s(u, v, lx, ly, _ppx, _ppy):
            if tilex:
                u = np.mod(lx, nw) / nw
            if tiley:
                v = np.mod(ly, nh) / nh
            if mir in ('Horizontal', 'Both'):
                u = 1.0 - u
            if mir in ('Vertical', 'Both'):
                v = 1.0 - v
            tx = (sub[0] + u * (sub[2] - sub[0])) * iw
            ty = (sub[1] + v * (sub[3] - sub[1])) * ih
            return _bilinear(img, tx, ty)
        return s

    def solid(self, rgba=(1, 1, 1, 1)):
        c = np.array(rgba, np.float32)

        def s(u, v, lx, ly, ppx, ppy):
            out = np.empty(u.shape + (4,), np.float32)
            out[:] = c
            return out
        return s

    # --- widgets -----------------------------------------------------------------------------------
    def draw_widget(self, cv_, w, M_parent, pw, ph, tint, placed=None):
        if not w.get('visible', 1):
            return
        t = w.get('$type')
        if placed is None:
            x, y, lw, lh = self.layout_in(w, pw, ph)
        else:
            x, y, lw, lh = placed
        M = M_parent @ T(x, y) @ local_transform(w, (lw, lh))
        tc = color(w.get('tintColor'))
        op = float(w.get('opacity', 1) if w.get('opacity') is not None else 1)
        tint = (tint[0] * tc[0], tint[1] * tc[1], tint[2] * tc[2], tint[3] * tc[3] * op)
        if tint[3] <= 1e-4 and t not in ('inkMaskWidget',):
            return
        name = cv(w.get('name'))
        lc = wdata(w.get('logicController') or {}) or {}
        if lc.get('$type') == 'inkCensorshipController':
            # censor bars: shown only with the game's censorship option on (default off)
            self.note('censorship widget %r hidden' % name)
            return
        if t == 'inkCanvasWidget':
            self.draw_canvas(cv_, w, M, lw, lh, tint)
        elif t == 'inkHorizontalPanelWidget':
            size, items = self.panel_layout(w)
            for c, cx, cy, cw, ch in items:
                self.draw_widget(cv_, c, M, size[0], size[1], tint, placed=(cx, cy, cw, ch))
        elif t == 'inkImageWidget':
            ap = depot(w.get('textureAtlas'))
            if w.get('useExternalDynamicTexture'):
                self.note('dynamic texture image %r skipped' % name)
                return
            if not ap:
                self.note('image %r has no atlas' % name)
                return
            a = self.ctx.atlas(ap)
            if a is None:
                self.note('atlas missing: %s' % ap.split(BS)[-1])
                return
            r = a.part(cv(w.get('texturePart')))
            if isinstance(r, str):
                self.note('image %r: %s' % (name, r))
                return
            arr, uv, nat = r
            self.images_drawn += 1
            self.draw_quad(cv_, M, lw, lh, self.tex_sampler(w, arr, uv, nat, lw, lh, M), tint)
        elif t == 'inkRectangleWidget':
            if tint[3] >= 0.9 and self.is_overlay(cv_, M, lw, lh):
                self.note('opaque full-frame rectangle %r above content skipped (transition start state)' % name)
                return
            self.draw_quad(cv_, M, lw, lh, self.solid(), tint)
        elif t == 'inkGradientWidget':
            self.draw_quad(cv_, M, lw, lh, self.gradient(w), tint)
        elif t == 'inkCircleWidget':
            def circ(u, v, lx, ly, ppx, ppy):
                d = np.sqrt((u - 0.5) ** 2 + (v - 0.5) ** 2) * 2
                a = np.clip((1 - d) * 0.5 * min(lw * ppx, lh * ppy) + 0.5, 0, 1)
                out = np.ones(u.shape + (4,), np.float32)
                return out * a[..., None]
            self.draw_quad(cv_, M, lw, lh, circ, tint)
        elif t == 'inkBorderWidget':
            th = float(w.get('thickness') or 1)

            def border(u, v, lx, ly, ppx, ppy):
                inside = (lx > th) & (lx < lw - th) & (ly > th) & (ly < lh - th)
                out = np.ones(u.shape + (4,), np.float32)
                return out * (~inside)[..., None]
            self.draw_quad(cv_, M, lw, lh, border, tint)
        elif t == 'inkQuadShapeWidget':
            self.draw_quadshape(cv_, w, M, tint)
        elif t == 'inkTextWidget':
            tx = self.text_of(w)
            if not tx:
                return
            im = self.text_image(w, tx)
            if im is None:
                return
            ih, iw = im.shape[:2]

            def ts(u, v, lx, ly, ppx, ppy):
                return _bilinear(im, lx, ly)
            self.draw_quad(cv_, M, max(lw, 1), max(lh, 1), ts, tint)
        elif t == 'inkVideoWidget':
            self.note('video %s left transparent' % (str(depot(w.get('videoResource'))).split(BS)[-1]))
        elif t == 'inkMaskWidget':
            pass  # handled by the parent canvas
        else:
            self.note('unhandled widget type %s' % t)

    def is_overlay(self, cv_, M, lw, lh):
        """True when the rect covers >=90% of the frame and content is already drawn beneath it: an opaque
        full-frame rectangle above content would hide the whole ad, so it is a flash/fade transition."""
        g = _grid(M, lw, lh, cv_.w, cv_.h)
        if g is None:
            return False
        y0, y1, x0, x1, lx, ly, cov, ppx, ppy = g
        if float(cov.sum()) < 0.9 * cv_.w * cv_.h:
            return False
        under = cv_.buf[y0:y1, x0:x1, 3]
        return float((under * cov).sum()) > 0.05 * float(cov.sum())

    def gradient(self, w):
        c0 = np.array(color(w.get('startColor')), np.float32)
        c1 = np.array(color(w.get('endColor')), np.float32)
        ang = math.radians(float(w.get('angle') or 0))
        mode = w.get('gradientMode') or 'Linear'
        # angle 0 runs startColor at the top edge to endColor at the bottom edge (verified on New Empire
        # 9x16: a 180-degree-rotated angle-0 gradient fades the photo into the white panel below it)
        dx, dy = -math.sin(ang), math.cos(ang)

        def s(u, v, lx, ly, ppx, ppy):
            if mode == 'Radial':
                f = np.clip(np.sqrt((u - 0.5) ** 2 + (v - 0.5) ** 2) * 2, 0, 1)
            elif mode == 'Rectangular':
                f = np.clip(np.maximum(np.abs(u - 0.5), np.abs(v - 0.5)) * 2, 0, 1)
            else:
                f = (u - 0.5) * dx + (v - 0.5) * dy
                f = np.clip(f / (abs(dx) * 0.5 + abs(dy) * 0.5 + 1e-6) * 0.5 + 0.5, 0, 1)
            col = c0 * (1 - f[..., None]) + c1 * f[..., None]
            col[..., :3] *= col[..., 3:4]
            return col
        return s

    def draw_quadshape(self, cv_, w, M, tint):
        vl = w.get('vertexList') or []
        pts = [vec(p) for p in vl]
        if len(pts) < 3:
            return
        if depot(w.get('textureAtlas')) and cv(w.get('texturePart')) not in (None, 'None'):
            self.note('textured quad shape drawn flat')
        P = [tuple((M @ np.array([px, py, 1.0]))[:2]) for px, py in pts]
        m = Image.new('L', (cv_.w * 2, cv_.h * 2), 0)
        ImageDraw.Draw(m).polygon([(px * 2, py * 2) for px, py in P], fill=255)
        a = np.asarray(m.resize((cv_.w, cv_.h), Image.BILINEAR), np.float32) / 255.0
        if not a.any():
            return
        tr, tg, tb, ta = tint
        col = np.stack([a * tr * ta, a * tg * ta, a * tb * ta, a * ta], -1).astype(np.float32)
        composite(cv_.buf, 0, cv_.h, 0, cv_.w, col)

    def mask_coverage(self, masks, M, lw, lh, W, H):
        cov = np.zeros((H, W), np.float32)
        for mw in masks:
            x, y, mwid, mh = self.layout_in(mw, lw, lh)
            MM = M @ T(x, y) @ local_transform(mw, (mwid, mh))
            g = _grid(MM, mwid, mh, W, H)
            if g is None:
                continue
            y0, y1, x0, x1, lx, ly, ecov, ppx, ppy = g
            a = ecov
            ap = depot(mw.get('textureAtlas'))
            if ap and 'white_mask_atlas' not in ap:
                at = self.ctx.atlas(ap)
                r = at.part(cv(mw.get('texturePart'))) if at else 'atlas missing'
                if isinstance(r, str):
                    self.note('mask %r: %s (treated as solid)' % (cv(mw.get('name')), r))
                else:
                    arr, uv, nat = r
                    s = self.tex_sampler(mw, arr, uv, nat, mwid, mh, MM)
                    a = a * s(lx / mwid, ly / mh, lx, ly, ppx, ppy)[..., 3]
            sub = cov[y0:y1, x0:x1]
            np.maximum(sub, a, out=sub)
        return cov

    def draw_canvas(self, cv_, w, M, lw, lh, tint):
        kids = children(w)
        if w.get('childOrder', 'Backward') != 'Forward':
            kids = kids[::-1]
        kids = self.drop_animation_layers(kids)
        masks = [c for c in kids if c.get('$type') == 'inkMaskWidget' and c.get('visible', 1)]
        if masks:
            layer = Canvas(cv_.w, cv_.h)
            for c in kids:
                if c.get('$type') != 'inkMaskWidget':
                    self.draw_widget(layer, c, M, lw, lh, tint)
            cov = self.mask_coverage(masks, M, lw, lh, cv_.w, cv_.h)
            inv = any(m.get('invertMask') for m in masks)
            if inv:
                cov = 1.0 - cov
            layer.buf *= cov[..., None]
            composite(cv_.buf, 0, cv_.h, 0, cv_.w, layer.buf)
            return
        for c in kids:
            self.draw_widget(cv_, c, M, lw, lh, tint)

    @staticmethod
    def _layout_key(c):
        L = c.get('layout') or {}
        m = L.get('margin') or {}
        rt = c.get('renderTransform') or {}
        r = lambda v: round(float(v or 0), 0)
        return (L.get('anchor'), tuple(r(m.get(k)) for k in ('left', 'top', 'right', 'bottom')),
                vec(L.get('anchorPoint')), r(rt.get('rotation')), vec(rt.get('scale'), 1, 1))

    @staticmethod
    def _parts(w):
        out = set()
        stack = [w]
        while stack:
            x = stack.pop()
            if not x.get('visible', 1):
                continue
            if x.get('$type') == 'inkImageWidget':
                out.add((depot(x.get('textureAtlas')), str(cv(x.get('texturePart')))))
            stack.extend(children(x))
        return frozenset(out)

    def drop_animation_layers(self, kids):
        """Animated ads stack several frames of one element at the same spot and let the animation pick
        one (cycling slogans, glitch copies of an image in other tints, slideshow canvases).  Static
        state: siblings with an identical layout that show the same atlas part (images) or are text
        widgets draw only once: images keep the copy whose tint is closest to white, text keeps the
        topmost.  In animated resources, sibling canvases with identical layout and size that hold
        different art are slides: only the bottom one is drawn."""
        groups = {}
        part_sets = set()
        for i, c in enumerate(kids):
            if not c.get('visible', 1):
                continue
            t = c.get('$type')
            if t == 'inkImageWidget':
                k = ('img', depot(c.get('textureAtlas')), str(cv(c.get('texturePart')))) + self._layout_key(c)
            elif t == 'inkTextWidget':
                k = ('txt',) + self._layout_key(c)
            elif t == 'inkCanvasWidget' and self.animated:
                parts = self._parts(c)
                if not parts or parts in part_sets:
                    continue  # no art, or the same art as a sibling (masked reveal bands etc.)
                part_sets.add(parts)
                k = ('cvs', vec(c.get('size'))) + self._layout_key(c)
            else:
                continue
            groups.setdefault(k, []).append(i)
        drop = set()
        for k, idx in groups.items():
            if len(idx) < 2:
                continue
            if k[0] == 'cvs':
                keep = min(idx)  # slides: the bottom one (drawn first) is the base/first state
            elif k[0] == 'img':
                def score(i):
                    tc = color(kids[i].get('tintColor'))
                    return (sum(abs(1 - x) for x in tc[:3]), -i)
                keep = min(idx, key=score)
            else:
                keep = max(idx)  # topmost
            drop.update(i for i in idx if i != keep)
            self.note('%d stacked animation layers of %r drawn once' % (len(idx), cv(kids[keep].get('name'))))
        return [c for i, c in enumerate(kids) if i not in drop]

    # --- text --------------------------------------------------------------------------------------
    def text_of(self, w):
        loc = (w.get('localizationString') or {}).get('value') or ''
        txt = w.get('text') or ''
        if loc and str(loc).startswith('LocKey'):
            if txt:
                return txt
            self.note('text %r uses %s (not resolved)' % (cv(w.get('name')), loc))
            return ''
        return txt

    _fonts = {}

    def text_image(self, w, txt):
        fam = str(depot(w.get('fontFamily')) or '')
        style = str(cv(w.get('fontStyle')) or 'Regular')
        size = max(4, int(round(float(w.get('fontSize') or 20))))
        cands = []
        win = os.path.join(os.environ['WINDIR'], 'Fonts') if os.environ.get('WINDIR') else None
        bold = 'bold' in style.lower() or 'semi' in style.lower() or 'medium' in style.lower()
        if 'orbitron' in fam:
            cands = ['bahnschrift.ttf', 'arialbd.ttf' if bold else 'arial.ttf']
        else:
            cands = ['bahnschrift.ttf', 'arialnb.ttf' if bold else 'arialn.ttf', 'arial.ttf']
        font = None
        for c in cands:
            key = (c, size)
            if key in self._fonts:
                font = self._fonts[key]
                break
            p = os.path.join(win, c) if win else None
            if p and os.path.exists(p):
                try:
                    font = ImageFont.truetype(p, size)
                    self._fonts[key] = font
                    break
                except OSError:
                    pass
        if font is None:
            font = ImageFont.load_default()
        if w.get('letterCase') == 'UpperCase':
            txt = txt.upper()
        lines = txt.split('\n')
        dummy = ImageDraw.Draw(Image.new('L', (1, 1)))
        lh = int(size * float(w.get('lineHeightPercentage') or 1.0) * 1.15)
        widths = [dummy.textlength(l, font=font) for l in lines]
        W = int(max(widths + [1])) + 2
        H = lh * len(lines) + 2
        im = Image.new('L', (W, H), 0)
        d = ImageDraw.Draw(im)
        just = str(w.get('justification') or w.get('textHorizontalAlignment') or 'Left')
        for i, l in enumerate(lines):
            x = 0
            if 'Center' in just:
                x = (W - widths[i]) / 2
            elif 'Right' in just:
                x = W - widths[i]
            d.text((x, i * lh), l, fill=255, font=font)
        a = np.asarray(im, np.float32) / 255.0
        self.note('text %r drawn with substitute font' % cv(w.get('name')))
        return np.stack([a, a, a, a], -1)


# ----------------------------------------------------------------------------------------------- api
def find_item(widget_json, library_item):
    rc = widget_json['Data']['RootChunk']
    for it in rc.get('libraryItems') or []:
        if str(cv(it.get('name'))) == str(library_item):
            pk = (it.get('package') or {}).get('Data')
            if pk:
                return pk['File']['RootChunk']
            chunks = (((it.get('packageData') or {}).get('Data') or {}).get('Chunks')) or []
            return chunks[0] if chunks else None
    return None


def render(inkwidget_json, library_item, root=None, scale=1.0, ctx=None, notes=None):
    """Render a library item.  inkwidget_json: path to a WolvenKit .inkwidget.json, or its loaded dict.
    root: the ads folder holding json/ and png/ (default: two levels above the json path; required with a
    loaded dict and no ctx).  Returns HxWx4 uint8 RGBA (straight alpha) at the root canvas size * scale.
    Raises LookupError when the item is absent."""
    if isinstance(inkwidget_json, str):
        if root is None:
            root = os.path.dirname(os.path.dirname(os.path.abspath(inkwidget_json)))
        with open(inkwidget_json, encoding='utf-8') as f:
            inkwidget_json = json.load(f)
    if ctx is None:
        if root is None:
            raise ValueError('render: root (the ads folder) is needed with a loaded inkwidget')
        ctx = Ctx(root)
    inst = find_item(inkwidget_json, library_item)
    if inst is None:
        raise LookupError('library item %r not found' % library_item)
    rw = wdata(inst.get('rootWidget'))
    if not rw:
        raise LookupError('library item %r has no root widget' % library_item)
    lc = wdata(rw.get('logicController') or {}) or {}
    src = str((inkwidget_json.get('Header') or {}).get('ArchiveFileName') or '').lower()
    animated = (lc.get('$type') == 'inkAnimatedAdvertController' or 'animated' in os.path.basename(src)
                or bool(inkwidget_json['Data']['RootChunk'].get('sequences')))
    r = Renderer(ctx, animated=animated)
    lw, lh = vec(rw.get('size'))
    if lw <= 0 or lh <= 0:
        raise ValueError('root widget %r has zero size' % cv(rw.get('name')))
    W, H = max(1, int(round(lw * scale))), max(1, int(round(lh * scale)))
    cvs = Canvas(W, H)
    M = mat(scale, 0, 0, 0, scale, 0)
    tc = color(rw.get('tintColor'))
    op = float(rw.get('opacity', 1) if rw.get('opacity') is not None else 1)
    # the root widget's own placement belongs to the device; render its content at the origin
    t = rw.get('$type')
    tint = (tc[0], tc[1], tc[2], tc[3] * op)
    if t == 'inkCanvasWidget':
        r.draw_canvas(cvs, rw, M, lw, lh, tint)
    else:
        r.draw_widget(cvs, rw, M, lw, lh, (1, 1, 1, 1), placed=(0, 0, lw, lh))
    if not r.images_drawn:
        r.note('no atlas artwork drawn (item holds only shapes/text: placeholder or text-only)')
    if notes is not None:
        notes.extend(r.notes)
    buf = cvs.buf
    a = np.clip(buf[..., 3:4], 0, 1)
    rgb = np.where(a > 1e-6, buf[..., :3] / np.maximum(a, 1e-6), 0)
    out = np.concatenate([np.clip(rgb, 0, 1), a], -1)
    return (out * 255 + 0.5).astype(np.uint8)


# ----------------------------------------------------------------------------------------------- cli
def _safe(s):
    return ''.join(c if c.isalnum() or c in '._-' else '_' for c in s)


def plan_jobs(root, ads):
    """-> list of jobs (resource, [(ad, fmt, libname, outname)]), failures {ad: {fmt: reason}}"""
    jdir = os.path.join(root, 'json')
    have = set(os.listdir(jdir))
    by_res = OrderedDict()
    fails = {}
    # output name: <LibraryName>.png for the preferred (static) resource, else <LibraryName>__<res>.png
    owners = {}
    for ad, rec in ads.items():
        res = rec.get('Resource') or ''
        for d in rec.get('Definitions') or []:
            lib = str(d.get('LibraryName') or '').strip()
            owners.setdefault(lib, set()).add(res)

    def pref(res):
        stem = res.replace('/', BS).split(BS)[-1].lower()
        return (1 if 'animated' in stem else 0, stem)
    primary = {lib: sorted(rs, key=pref)[0] for lib, rs in owners.items()}
    for ad, rec in ads.items():
        res = rec.get('Resource') or ''
        stem = res.replace('/', BS).split(BS)[-1]
        for d in rec.get('Definitions') or []:
            fmt = str(d.get('Format') or '').strip()
            lib = str(d.get('LibraryName') or '').strip()
            if not res:
                fails.setdefault(ad, {})[fmt + '/' + lib] = 'no Resource'
                continue
            if res.startswith('0x'):
                fails.setdefault(ad, {})[fmt + '/' + lib] = 'resource is an unresolved hash %s' % res
                continue
            if stem + '.json' not in have:
                fails.setdefault(ad, {})[fmt + '/' + lib] = 'inkwidget not extracted: %s' % res
                continue
            if primary.get(lib) == res:
                out = _safe(lib) + '.png'
            else:
                out = _safe(lib) + '__' + _safe(stem.rsplit('.', 1)[0]) + '.png'
            by_res.setdefault(res, []).append((ad, fmt, lib, out))
    return list(by_res.items()), fails


def run_job(args):
    root, outdir, res, defs, scale = args
    ctx = Ctx(root)
    results = []
    d = ctx.load_json(res)
    if d is None:
        why = 'inkwidget not extracted: %s (json/ only holds a same-named file from another folder)' % res
        return res, [(ad, fmt, lib, None, why, []) for ad, fmt, lib, out in defs]
    done = {}
    for ad, fmt, lib, out in defs:
        if out in done:
            results.append((ad, fmt, lib) + done[out])
            continue
        notes = []
        try:
            img = render(d, lib, root=root, scale=scale, ctx=ctx, notes=notes)
            if img[..., 3].max() == 0:
                r = (None, 'rendered fully transparent' + ('; ' + '; '.join(notes[:4]) if notes else ''), notes)
            else:
                Image.fromarray(img).save(os.path.join(outdir, out), optimize=False)
                r = (out, None, notes)
        except Exception as e:  # noqa: BLE001 - record any failure per definition
            r = (None, '%s: %s' % (type(e).__name__, e), notes)
        done[out] = r
        results.append((ad, fmt, lib) + r)
    return res, results


def contact_sheet(outdir, manifest, path, n=24, cell=320, seed=7):
    """pick ~n varied ads (cycling formats), lay out with labels."""
    import random
    rnd = random.Random(seed)
    items = []
    for ad, fm in manifest.items():
        if ad.startswith('_'):
            continue
        for fmt, p in fm.items():
            items.append((fmt, ad, p))
    rnd.shuffle(items)
    fmts = ['Format_16x9', 'Format_4x3', 'Format_3_3x1', 'Format_9x16', 'Format_1x1', 'Format_21x9',
            'Format_3x4', 'Format_9x21', 'Format_1x3_3']
    pick, seen = [], set()
    while len(pick) < n and items:
        progressed = False
        for f in fmts:
            for i, (fmt, ad, p) in enumerate(items):
                base_ad = ad.replace('_Animated', '')
                if fmt == f and base_ad not in seen:
                    pick.append(items.pop(i))
                    seen.add(base_ad)
                    progressed = True
                    break
            if len(pick) >= n:
                break
        if not progressed:
            break
    cols = 6
    rows = (len(pick) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * cell, rows * (cell + 18)), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    for i, (fmt, ad, p) in enumerate(pick):
        im = Image.open(os.path.join(outdir, p)).convert('RGBA')
        im.thumbnail((cell - 8, cell - 8), Image.LANCZOS)
        bg = Image.new('RGBA', im.size, (255, 0, 255, 255))  # magenta shows transparency
        bg.alpha_composite(im)
        x = (i % cols) * cell + (cell - im.size[0]) // 2
        y = (i // cols) * (cell + 18) + (cell - im.size[1]) // 2
        sheet.paste(bg.convert('RGB'), (x, y))
        dr.text(((i % cols) * cell + 4, (i // cols) * (cell + 18) + cell + 2), '%s %s' % (ad.split('.')[-1], fmt[7:]),
                fill=(230, 230, 230))
    sheet.save(path, quality=88)
    return [p for _, _, p in pick]


def set_idle_priority():
    """idle CPU priority (inherited by worker processes) so renders never compete with a running game"""
    try:
        if os.name == 'nt':
            import ctypes
            k = ctypes.windll.kernel32
            k.SetPriorityClass(k.GetCurrentProcess(), 0x40)  # IDLE_PRIORITY_CLASS
        else:
            os.nice(19)
    except Exception:  # noqa: BLE001 - priority is best effort
        pass


def render_all(a, root):
    """rendered/ (or a.out): every definition of every ad in <root>/ads.json (a.only filters by name)"""
    outdir = os.path.abspath(a.out or os.path.join(root, 'rendered'))
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(root, 'ads.json'), encoding='utf-8') as f:
        ads = json.load(f)['ads']
    if a.only:
        ads = {k: v for k, v in ads.items() if a.only.lower() in k.lower()}
    jobs, fails = plan_jobs(root, ads)
    # big resources first for balance
    jobs.sort(key=lambda j: -os.path.getsize(os.path.join(root, 'json', j[0].split(BS)[-1] + '.json')))
    notes_out = OrderedDict()
    work = [(root, outdir, res, defs, a.scale) for res, defs in jobs]
    if a.jobs > 1 and len(work) > 1:
        from multiprocessing import Pool
        with Pool(a.jobs) as pool:
            it = pool.imap_unordered(run_job, work)
            results = list(it)
    else:
        results = [run_job(w) for w in work]
    ok = 0
    rel = (lambda o: 'rendered/' + o) if outdir == os.path.join(root, 'rendered') else (lambda o: os.path.join(outdir, o))
    done_ok = set()
    per_fmt = OrderedDict()
    for res, rs in results:
        for ad, fmt, lib, out, err, notes in rs:
            key = fmt + '/' + lib
            if out:
                done_ok.add((ad, key))
                per_fmt.setdefault(ad, OrderedDict()).setdefault(fmt, []).append(rel(out))
                ok += 1
                if notes:
                    notes_out.setdefault(ad, OrderedDict())[key] = notes
            else:
                fails.setdefault(ad, {})[key] = err
    total = sum(len(v.get('Definitions') or []) for v in ads.values())
    # {ad: {format: png}} uses the ad's first definition of each format; ads with several
    # definitions (art variants) of one format list them all under _variants
    ordered = OrderedDict()
    variants = OrderedDict()
    for ad in sorted(per_fmt):
        ordered[ad] = OrderedDict((f, ps[0]) for f, ps in per_fmt[ad].items())
        for f, ps in per_fmt[ad].items():
            if len(ps) > 1:
                variants.setdefault(ad, OrderedDict())[f] = ps
    ordered['_variants'] = variants
    ordered['_failures'] = OrderedDict(sorted(fails.items()))
    # animated renders whose LibraryName also renders from a static resource -> that static png
    # (the static resource shows the finished artwork without stacked animation frames)
    static_png = {}
    for res, defs in jobs:
        if 'animated' not in res.lower():
            for ad, fmt, lib, out in defs:
                if (ad, fmt + '/' + lib) in done_ok:
                    static_png[lib] = rel(out)
    eq = OrderedDict()
    for res, defs in jobs:
        if 'animated' in res.lower():
            for ad, fmt, lib, out in defs:
                if lib in static_png and (ad, fmt + '/' + lib) in done_ok:
                    eq[rel(out)] = static_png[lib]
    ordered['_static_equivalents'] = OrderedDict(sorted(eq.items()))
    ordered['_notes'] = OrderedDict(sorted(notes_out.items()))
    ordered['_summary'] = {'definitions': total, 'rendered': ok, 'failed': total - ok,
                           'paths': 'relative to the ads folder (%s)' % root}
    with open(os.path.join(outdir, 'manifest.json'), 'w', encoding='utf-8') as f:
        json.dump(ordered, f, indent=1)
    print('rendered %d / %d definitions (%.1f%%)' % (ok, total, 100.0 * ok / max(1, total)))
    if a.sheet != 'none' and per_fmt:
        sp = a.sheet or os.path.join(outdir, 'sheet.jpg')
        sheet_in = {ad: {f: os.path.basename(p) for f, p in fm.items()} for ad, fm in ordered.items()
                    if not ad.startswith('_')}
        contact_sheet(outdir, sheet_in, sp)
        print('sheet', sp)
    return 0


# ----------------------------------------------------------------------------------------------- dump, fetch
ADVERTS_RE = r'base\\gameplay\\gui\\world\\adverts\\.*\.(inkwidget|inkatlas|xbm)$'
ADVERTS_TEX_RE = r'base\\gameplay\\gui\\world\\adverts\\.*\.xbm$'


def game_running():
    """True when Cyberpunk2077.exe is running (Windows; elsewhere False)"""
    if os.name != 'nt':
        return False
    r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq Cyberpunk2077.exe', '/NH'], capture_output=True, text=True,
                       errors='replace')
    return 'cyberpunk2077.exe' in r.stdout.lower()


def step_dump(root):
    """ads.json from the game's tweakdb.bin (cp77_ads_dump.ps1)"""
    wk = lpaths.need('WOLVENKIT_CLI').parent
    db = lpaths.need('CP77_GAME') / 'r6' / 'cache' / 'tweakdb.bin'
    if not db.exists():
        sys.exit('no %s' % db)
    r = subprocess.run([lpaths.pwsh(), '-NoProfile', '-File', str(HERE / 'cp77_ads_dump.ps1'), '-Wk', str(wk),
                        '-Db', str(db), '-Out', str(root / 'ads.json')], capture_output=True, text=True, errors='replace')
    print(r.stdout.strip(), flush=True)
    if r.returncode:
        sys.exit('cp77_ads_dump.ps1 failed (%d):\n%s' % (r.returncode, r.stderr[-2000:]))


def step_fetch(root):
    """raw/, json/, png/ from the game's archives (WolvenKit unbundle, convert serialize, uncook)"""
    exe = lpaths.need('WOLVENKIT_CLI')
    env = dict(os.environ, DOTNET_ROOT=str(lpaths.need('WOLVENKIT_DOTNET')))
    game = lpaths.need('CP77_GAME')
    arch = [str(game / 'archive' / 'pc' / 'content'), str(game / 'archive' / 'pc' / 'ep1')]
    raw, jdir, pdir = root / 'raw', root / 'json', root / 'png'
    for d in (raw, jdir, pdir):
        d.mkdir(parents=True, exist_ok=True)
    with open(root / 'fetch_ads.log', 'w', encoding='utf-8', errors='replace') as log:
        def wk(title, *args):
            log.write('== %s %s\n' % (title, time.strftime('%H:%M:%S')))
            log.flush()
            r = subprocess.run([str(exe), *args, '-v', 'Minimal'], env=env, stdout=log, stderr=subprocess.STDOUT)
            log.write('exit=%d\n' % r.returncode)
            log.flush()
            if r.returncode:
                sys.exit('WolvenKit %s failed (%d): see %s' % (args[0], r.returncode, root / 'fetch_ads.log'))

        def count(d, suffix=''):
            return sum(1 for f in d.rglob('*') if f.is_file() and f.name.endswith(suffix))
        wk('unbundle', 'unbundle', *arch, '-r', ADVERTS_RE, '-o', str(raw))
        log.write('files: %d\n' % count(raw))
        wk('serialise widgets and atlases', 'convert', 'serialize', str(raw), '-o', str(jdir))
        log.write('json: %d\n' % count(jdir))
        wk('export textures', 'uncook', *arch, '-r', ADVERTS_TEX_RE, '-o', str(pdir), '--uext', 'png')
        log.write('png: %d\n' % count(pdir, '.png'))
        log.write('ALL_DONE %s\n' % time.strftime('%H:%M:%S'))
    print('fetch: raw %d files, json %d, png %d' % (count(raw), count(jdir), count(pdir, '.png')), flush=True)


STEPS = ('dump', 'fetch', 'render')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('root', help='the ads folder (CP77_ADS_DIR): ads.json, raw/, json/, png/, rendered/')
    ap.add_argument('--steps', default=','.join(STEPS), help='comma-separated, of %s (default all)' % ', '.join(STEPS))
    ap.add_argument('--out', default=None, help='render output folder (default <root>/rendered)')
    ap.add_argument('--only', default=None, help='render only ads whose name contains this text')
    ap.add_argument('--jobs', type=int, default=max(1, min(6, (os.cpu_count() or 2) - 2)))
    ap.add_argument('--scale', type=float, default=1.0)
    ap.add_argument('--sheet', default=None, help='contact sheet path (default <out>/sheet.jpg); "none" skips')
    ap.add_argument('--one', nargs=3, metavar=('JSON', 'ITEM', 'PNG'), help='render one library item')
    ap.add_argument('--normal-priority', action='store_true', help='do not drop to idle CPU priority')
    a = ap.parse_args(argv)
    root = Path(os.path.abspath(a.root))
    if a.one:
        if not a.normal_priority:
            set_idle_priority()
        notes = []
        img = render(a.one[0], a.one[1], root=str(root), scale=a.scale, notes=notes)
        Image.fromarray(img).save(a.one[2])
        print('wrote', a.one[2], img.shape, *notes, sep='\n  ')
        return 0
    steps = [s.strip() for s in a.steps.split(',') if s.strip()]
    bad = [s for s in steps if s not in STEPS]
    if bad:
        sys.exit('unknown step(s) %s (steps: %s)' % (', '.join(bad), ', '.join(STEPS)))
    root.mkdir(parents=True, exist_ok=True)
    if ('dump' in steps or 'fetch' in steps) and game_running():
        sys.exit('Cyberpunk2077.exe is running: WolvenKit reads its files; run the dump and fetch steps after it exits')
    for s in STEPS:
        if s not in steps:
            continue
        t0 = time.time()
        if s == 'dump':
            step_dump(root)
        elif s == 'fetch':
            step_fetch(root)
        else:
            if not a.normal_priority:
                set_idle_priority()
            render_all(a, str(root))
        print('%s: %.1f s' % (s, time.time() - t0), flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
