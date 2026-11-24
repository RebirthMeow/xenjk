"""colbrushes.py -- q3map2 -brushbin's brush steps in Python: a collision chunk's brushes without a q3map2 process.

colcook.py hands each tile's brush records (per side three plane points as doubles and a shader) to q3map2 build
xenjk9's `-brushbin` (tools/levelport/q3map2/brush_bin.patch) and reads back the tile's shaders, planes, brushes and
sides. `tile()` here returns the same four arrays, byte for byte, from the same records, by doing what that build does
with `-bsp -meta -exactplanes -pointgrid 16384 -game ja` (netradiant-custom be3b369 with the README's patches; the
build is mingw x86-64, -O3, SSE2 scalar float and double, no FMA, so each C++ operation is one IEEE operation here):

- BinRawBrush: the points on the 1/16384 grid, PlaneFromPoints in double, the float plane, SnapPlaneImproved (the
  SNAP_NORMAL_FIX SnapNormal; with -exactplanes the distance through the points' centre), FindFloatPlane with
  -exactplanes (a plane is reused only when exactly equal and the points lie within distanceEpsilon of it, the newest
  such plane first; -0 stored as +0; each new plane with its flipped twin, axial planes positive first);
- ParseBrush: degenerate, duplicate and mirrored planes, SetBrushContents;
- FinishBrush: CreateBrushWindings (OFFSET_WINDING_CREATION: the vertices of every three planes inside all of them
  bound the base windings, which are cut in double about the bounds' centre, welded by FixWindingAccu and kept as
  float), BoundBrush (and the +-65,536 world), AddBrushBevels with -exactplanes (box bevels at the bounds of the
  brush's own double windings in a 64-unit anchored frame, edge bevels from those windings);
- BinEmit: EmitBrushes' order (opaque brushes reversed, then the translucent) and EmitShader's table (a side's shader
  with its flags, a bevel as "noshader" with its surface flags).

How it runs: the sides' planes at once (side_planes, elementwise numpy); each brush's steps on plane values only
(brush_geometry: with -exactplanes nothing a brush computes reads a plane's number, only its value, and its
duplicate and mirrored planes follow from the values and point tests alone), in worker processes by batches; then
the plane numbers for every lookup in q3map2's order (number_planes: the table's reuse rule with sorts and running
maxima) and the brush, side and shader arrays. Scalar float steps are Python floats rounded through struct (f32).

The shaders' flags come from the shader scripts q3map2 would read (ShaderTable.from_basepaths: the game's
`<basepath>/base` folders and their pk3s, in q3map2's VFS order; the `ja` surfaceparms below), parsed for the
directives that set flags (surfaceparm, q3map_material, q3map_<surfaceparm>, the sky and autosprite cases,
q3map_baseShader, q3map_deprecateShader, damageShader). The parsed table is cached by the files' sizes and times.

Gate: `python tools/levelport/colbrushes.py gate <records.npz>` runs a colcook dump (gltf2map --cook --cook-gate
writes one beside its report) through Python against the q3map2 tiles in it and compares every tile's arrays;
gltf2map --cook --cook-gate does that and the two chunk BSPs inline. `colbrushes.py collision-glb` makes a test
input of a gameplay glb (only its collision nodes: a cooked chunk is brushes only).
"""
import hashlib
import json
import math
import os
import pickle
import struct
import sys
import time
import zipfile
from itertools import combinations
from pathlib import Path

import numpy as np

if __name__ == '__main__':
    sys.path.insert(0, str(Path(__file__).resolve().parent))
from colbsp import BRUSH, PLANE, SHADER, SIDE  # noqa: E402

# ---------------------------------------------------------------- q3map2's constants (q3map2.h, games.cpp, polylib)

POINT_GRID = 16384.0                    # -pointgrid 16384
NORMAL_EPSILON = 0.00005                # q3map2.h, SNAP_NORMAL_FIX
DISTANCE_EPSILON = 0.005                # q3map2.h, HIGH_PRECISION_MATH_FIXES
ON_EPSILON = 0.1                        # polylib.h
DEGENERATE_EPSILON = 0.1                # brush.cpp
SNAP_EPSILON = 0.01                     # brush.cpp
CHOP_EPSILON = 2.0 ** -24               # ChopWindingInPlaceAccu: FLT_EPSILON * 0.5 (crude epsilon 0)
MAX_WORLD = 65536.0                     # maxworld.h (xenjk9 has no world_coords.patch)
MAX_BUILD_SIDES = 1024
MAX_POINTS_ON_WINDING = 512
DBL_MAX = sys.float_info.max
FLT_MAX = float(np.finfo(np.float32).max)

_F = struct.Struct('<f')


def f32(x):
    """x rounded to float (a C++ float assignment)"""
    return _F.unpack(_F.pack(x))[0]


_F3 = struct.Struct('<3f')
_F4 = struct.Struct('<4f')


def f32x3(a, b, c):
    return _F3.unpack(_F3.pack(a, b, c))


F_00025 = f32(0.00025)          # SnapNormal's 0.00025f
F_01 = f32(0.1)                 # AddBrushBevels' .1f

# compile flags (games.h)
C_SOLID, C_TRANSLUCENT, C_STRUCTURAL, C_HINT = 0x1, 0x2, 0x4, 0x8
C_NODRAW, C_LIGHTGRID, C_ALPHASHADOW, C_LIGHTFILTER = 0x10, 0x20, 0x40, 0x80
C_VERTEXLIT, C_LIQUID, C_FOG, C_SKY, C_ORIGIN, C_AREAPORTAL = 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000
C_ANTIPORTAL, C_SKIP, C_NOMARKS, C_OB, C_DETAIL = 0x4000, 0x8000, 0x10000, 0x20000, 0x08000000

# game_sof2 / game_ja (games.cpp): contents, surface flags
S_CONT_SOLID, S_CONT_LAVA, S_CONT_WATER, S_CONT_FOG = 0x1, 0x2, 0x4, 0x8
S_CONT_PLAYERCLIP, S_CONT_MONSTERCLIP, S_CONT_BOTCLIP, S_CONT_SHOTCLIP = 0x10, 0x20, 0x40, 0x80
S_CONT_TRIGGER, S_CONT_NODROP, S_CONT_TERRAIN, S_CONT_LADDER, S_CONT_ABSEIL = 0x400, 0x800, 0x1000, 0x2000, 0x4000
S_CONT_OPAQUE, S_CONT_OUTSIDE, S_CONT_SLIME = 0x8000, 0x10000, 0x20000
S_CONT_DETAIL, S_CONT_TRANSLUCENT = 0x08000000, -0x80000000          # (0x80000000 as a C int)
S_SURF_SKY, S_SURF_SLICK, S_SURF_METALSTEPS, S_SURF_FORCEFIELD = 0x2000, 0x4000, 0x8000, 0x10000
S_SURF_NODAMAGE, S_SURF_NOIMPACT, S_SURF_NOMARKS, S_SURF_NODRAW = 0x40000, 0x80000, 0x100000, 0x200000
S_SURF_NOSTEPS, S_SURF_NODLIGHT, S_SURF_NOMISCENTS = 0x400000, 0x800000, 0x1000000
BEVELS_MASK = S_SURF_SLICK | S_SURF_METALSTEPS | S_SURF_NODAMAGE | S_SURF_NOSTEPS | S_SURF_NOMISCENTS
S_MAT_MASK = 0x1f
_SC = S_CONT_SOLID | S_CONT_OPAQUE
# name: (content, content clear, surface, surface clear, compile, compile clear); -1 clears every bit
SURFACE_PARMS = {
    'default': (_SC, -1, 0, -1, C_SOLID, -1),
    'lightgrid': (0, 0, 0, 0, C_LIGHTGRID, 0),
    'antiportal': (0, 0, 0, 0, C_ANTIPORTAL, 0),
    'skip': (0, 0, 0, 0, C_SKIP, 0),
    'origin': (0, S_CONT_SOLID, 0, 0, C_ORIGIN | C_TRANSLUCENT, C_SOLID),
    'areaportal': (S_CONT_TRANSLUCENT, _SC, 0, 0, C_AREAPORTAL | C_TRANSLUCENT, C_SOLID),
    'trans': (S_CONT_TRANSLUCENT, 0, 0, 0, C_TRANSLUCENT, 0),
    'detail': (S_CONT_DETAIL, 0, 0, 0, C_DETAIL, 0),
    'structural': (0, 0, 0, 0, C_STRUCTURAL, 0),
    'hint': (0, 0, 0, 0, C_HINT, 0),
    'nodraw': (0, 0, S_SURF_NODRAW, 0, C_NODRAW, 0),
    'alphashadow': (0, 0, 0, 0, C_ALPHASHADOW | C_TRANSLUCENT, 0),
    'lightfilter': (0, 0, 0, 0, C_LIGHTFILTER | C_TRANSLUCENT, 0),
    'nolightmap': (0, 0, 0, 0, C_VERTEXLIT, 0),
    'pointlight': (0, 0, 0, 0, C_VERTEXLIT, 0),
    'nonsolid': (0, S_CONT_SOLID, 0, 0, 0, C_SOLID),
    'nonopaque': (0, S_CONT_OPAQUE, 0, 0, C_TRANSLUCENT, 0),
    'trigger': (S_CONT_TRIGGER, _SC, 0, 0, C_TRANSLUCENT, C_SOLID),
    'water': (S_CONT_WATER, S_CONT_SOLID, 0, 0, C_LIQUID | C_TRANSLUCENT, C_SOLID),
    'slime': (S_CONT_SLIME, S_CONT_SOLID, 0, 0, C_LIQUID | C_TRANSLUCENT, C_SOLID),
    'lava': (S_CONT_LAVA, S_CONT_SOLID, 0, 0, C_LIQUID | C_TRANSLUCENT, C_SOLID),
    'shotclip': (S_CONT_SHOTCLIP, _SC, 0, 0, C_DETAIL | C_TRANSLUCENT, C_SOLID),
    'playerclip': (S_CONT_PLAYERCLIP, _SC, 0, 0, C_DETAIL | C_TRANSLUCENT, C_SOLID),
    'monsterclip': (S_CONT_MONSTERCLIP, _SC, 0, 0, C_DETAIL | C_TRANSLUCENT, C_SOLID),
    'nodrop': (S_CONT_NODROP, _SC, 0, 0, C_DETAIL | C_TRANSLUCENT, C_SOLID),
    'terrain': (S_CONT_TERRAIN, _SC, 0, 0, C_DETAIL | C_TRANSLUCENT, C_SOLID),
    'ladder': (S_CONT_LADDER, _SC, 0, 0, C_DETAIL | C_TRANSLUCENT, C_SOLID),
    'abseil': (S_CONT_ABSEIL, _SC, 0, 0, C_DETAIL | C_TRANSLUCENT, C_SOLID),
    'outside': (S_CONT_OUTSIDE, _SC, 0, 0, C_DETAIL | C_TRANSLUCENT, C_SOLID),
    'botclip': (S_CONT_BOTCLIP, _SC, 0, 0, C_DETAIL | C_TRANSLUCENT, C_SOLID),
    'fog': (S_CONT_FOG, _SC, 0, 0, C_FOG | C_DETAIL | C_TRANSLUCENT, C_SOLID),
    'sky': (0, 0, S_SURF_SKY, 0, C_SKY, 0),
    'slick': (0, 0, S_SURF_SLICK, 0, 0, 0),
    'noimpact': (0, 0, S_SURF_NOIMPACT, 0, 0, 0),
    'nomarks': (0, 0, S_SURF_NOMARKS, 0, C_NOMARKS, 0),
    'nodamage': (0, 0, S_SURF_NODAMAGE, 0, 0, 0),
    'metalsteps': (0, 0, S_SURF_METALSTEPS, 0, 0, 0),
    'nosteps': (0, 0, S_SURF_NOSTEPS, 0, 0, 0),
    'nodlight': (0, 0, S_SURF_NODLIGHT, 0, 0, 0),
    'nomiscents': (0, 0, S_SURF_NOMISCENTS, 0, 0, 0),
    'forcefield': (0, 0, S_SURF_FORCEFIELD, 0, 0, 0),
}
for _k, _m in enumerate(('none', 'solidwood', 'hollowwood', 'solidmetal', 'hollowmetal', 'shortgrass', 'longgrass',
                         'dirt', 'sand', 'gravel', 'glass', 'concrete', 'marble', 'water', 'snow', 'ice', 'flesh',
                         'mud', 'bpglass', 'dryleaves', 'greenleaves', 'fabric', 'canvas', 'rock', 'rubber',
                         'plastic', 'tiles', 'carpet', 'plaster', 'shatterglass', 'armor', 'computer')):
    SURFACE_PARMS['*mat_' + _m] = (0, 0, _k, S_MAT_MASK, 0, 0)
SURFACE_PARMS['inside'] = (0x10000000, 0, 0, 0, 0, 0)         # game_ja
SURFACE_PARMS['forcesight'] = (0, 0, 0x02000000, 0, 0, 0)

# the q3map_ directives ParseShaderFile handles itself (any other q3map_X applies surfaceparm X)
Q3MAP_KNOWN = {t.lower() for t in (
    'q3map_baseShader q3map_surfacemodel q3map_foliage q3map_bounce q3map_bounceScale q3map_skyLight '
    'q3map_surfacelight q3map_lightStyle q3map_lightRGB q3map_lightSubdivide q3map_backsplash q3map_floodLight '
    'q3map_nodirty q3map_lightmapSampleSize q3map_lightmapSampleOffset q3map_lightmapFilterRadius q3map_lightmapAxis '
    'q3map_lightmapSize q3map_lightmapBrightness q3map_lightmapGamma q3map_vertexScale q3map_noVertexLight '
    'q3map_flare q3map_flareShader q3map_backShader q3map_cloneShader q3map_remapShader q3map_deprecateShader '
    'q3map_offset q3map_fur q3map_terrain q3map_forceMeta q3map_shadeAngle q3map_textureSize q3map_tcGen '
    'q3map_colorGen q3map_colorMod q3map_tcMod q3map_fogDir q3map_globaltexture q3map_nonplanar q3map_noclip '
    'q3map_notjunc q3map_nofog q3map_indexed q3map_invert q3map_lightmapMergable q3map_noFast q3map_patchShadows '
    'q3map_vertexShadows q3map_noVertexShadows q3map_splotchfix q3map_forceSunlight q3map_onlyVertexLighting '
    'q3map_material q3map_clipmodel q3map_styleMarker q3map_styleMarker2').split()}


def _s32(v):
    """an int as C's 32-bit int"""
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v


# ---------------------------------------------------------------- shader scripts (shaders.cpp)

class ShaderInfo:
    __slots__ = ('shader', 'content', 'surface', 'compile', 'deprecate', 'damage')

    def __init__(self, name):
        self.shader = name
        self.content = self.surface = self.compile = 0
        self.deprecate = self.damage = None
        self.apply('default')            # AllocShaderInfo

    def apply(self, parm):
        sp = SURFACE_PARMS.get(parm.lower())
        if sp is None:
            return False
        cf, cc, sf, sc, kf, kc = sp
        self.content = _s32((self.content & ~cc) | cf)
        self.surface = _s32((self.surface & ~sc) | sf)
        self.compile = _s32((self.compile & ~kc) | kf)
        return True


def tokens(text):
    """q3map2's GetToken over a script: [(token, line)] ('; # //' and '/* */' comments; bytes <= 32 and, chars
    being signed, bytes >= 128 are space; a quoted token keeps its line)."""
    out = []
    b = text if isinstance(text, bytes) else text.encode('latin-1', 'replace')
    n, i, line = len(b), 0, 1
    while i < n:
        c = b[i]
        if c <= 32 or c >= 128:
            if c == 10:
                line += 1
            i += 1
            continue
        if c == 59 or c == 35 or (c == 47 and i + 1 < n and b[i + 1] == 47):        # ; # //
            j = b.find(b'\n', i)
            if j < 0:
                break
            i = j + 1
            line += 1
            continue
        if c == 47 and i + 1 < n and b[i + 1] == 42:                                # /*
            j = b.find(b'*/', i + 2)
            if j < 0:
                break
            line += b.count(b'\n', i, j)
            i = j + 2
            continue
        if c == 34:                                                                 # "..."
            j = b.find(b'"', i + 1)
            if j < 0:
                out.append((b[i + 1:].decode('latin-1'), line))
                break
            out.append((b[i + 1:j].decode('latin-1'), line))
            i = j + 1
            continue
        j = i
        while j < n and 32 < b[j] < 128 and b[j] != 59:
            j += 1
        out.append((b[i:j].decode('latin-1'), line))
        i = j
    return out


def _extensionless(name):
    """PathExtensionless: the name without an extension after its last path separator"""
    s = max(name.rfind('/'), name.rfind('\\'))
    d = name.rfind('.')
    return name[:d] if d > s else name


class ShaderTable:
    """q3map2's shaderInfo list: the definitions in parse order, the first of a name (any case) wins."""

    def __init__(self):
        self.infos = []
        self.index = {}             # lower name -> first ShaderInfo

    def _alloc(self, name):
        si = ShaderInfo(name)
        self.infos.append(si)
        self.index.setdefault(name.lower(), si)
        return si

    def parse(self, text):
        """ParseShaderFile, for the flags"""
        tok = tokens(text)
        n, p = len(tok), 0

        def avail(line):
            return p < n and tok[p][1] == line

        while p < n:
            name, _ = tok[p]
            p += 1
            if name.lower().endswith(':q3map'):
                name = name[:-6]
            si = self._alloc(name)
            if p >= n or tok[p][0] != '{':
                raise ValueError('shader %s: { not found' % name)
            p += 1
            while p < n and tok[p][0] != '}':
                t, line = tok[p]
                p += 1
                tl = t.lower()
                skip_line = True
                if t == '{':
                    while p < n and tok[p][0] != '}':
                        p += 1
                    p += 1
                    line = tok[p - 1][1] if p - 1 < n else line
                elif tl == 'surfaceparm':
                    if avail(line):
                        si.apply(tok[p][0])
                        p += 1
                elif tl == 'deformvertexes':
                    if avail(line):
                        if tok[p][0].lower().startswith('autosprite'):
                            si.apply('detail')
                        p += 1
                elif tl == 'damageshader':
                    if avail(line):
                        if tok[p][0]:
                            si.damage = tok[p][0]
                        p += 1
                elif tl in ('sun', 'q3map_sun', 'q3map_sunext'):
                    for _ in range(6):
                        if avail(line):
                            p += 1
                    if tl == 'q3map_sunext' and avail(line):
                        p += 2 if avail(line) else 0
                    si.apply('sky')
                    skip_line = False
                elif tl == 'q3map_skylight':
                    for _ in range(2):
                        if avail(line):
                            p += 1
                    si.apply('sky')
                elif tl.startswith('q3map_') and tl not in ('q3map_tesssize', 'q3map_normalimage', 'q3map_lightimage'):
                    if tl == 'q3map_baseshader':
                        if avail(line):
                            base = self.lookup(tok[p][0])
                            p += 1
                            for k in ShaderInfo.__slots__:
                                setattr(si, k, getattr(base, k))
                    elif tl == 'q3map_deprecateshader':
                        if avail(line):
                            si.deprecate = tok[p][0]
                            p += 1
                    elif tl == 'q3map_onlyvertexlighting':
                        si.apply('pointlight')
                    elif tl == 'q3map_material':
                        if avail(line):
                            si.apply('*mat_' + tok[p][0])
                            p += 1
                    elif tl not in Q3MAP_KNOWN:
                        si.apply(t[6:])
                if skip_line:
                    while avail(line):
                        p += 1
            p += 1              # the closing }

    def lookup(self, name):
        """ShaderInfoForShader: the extension dropped, the first definition (deprecations followed), else a default
        shader allocated under the name"""
        shader = _extensionless(name or 'missing')
        depth = 0
        while True:
            si = self.index.get(shader.lower())
            if si is None:
                break
            if depth < 16 and si.deprecate:
                shader = _extensionless(si.deprecate)
                depth += 1
                continue
            return si
        return self._alloc(shader)

    @staticmethod
    def from_texts(texts):
        t = ShaderTable()
        for x in texts:
            t.parse(x)
        return t

    @staticmethod
    def from_basepaths(basepaths, game='base', cache=None):
        """The shader files q3map2 -game ja loads from -fs_basepath <each>: vfsInitDirectory(<basepath>/<game>) in
        order (pk3s sorted ascending, every pk3's files prepended to one list), shaders/shaderlist.txt if any else
        vfsListShaderFiles (each folder's shaders/*.shader, then the packs' list), each file read from its first
        place (the folders, then the packs' list). cache: a folder for the parsed table, keyed by the files' sizes and
        times."""
        dirs = [Path(bp) / game for bp in basepaths]
        loose, paks = [], []
        stamp = []
        for d in dirs:
            if not d.is_dir():
                continue
            sd = d / 'shaders'
            if sd.is_dir():
                names = sorted(os.listdir(sd), key=str.lower)
                for f in names:
                    if f.lower().endswith('.shader'):
                        st = (sd / f).stat()
                        stamp.append((str(sd / f), st.st_size, st.st_mtime_ns))
                loose.append((sd, names))
            pk = [f for f in os.listdir(d) if f.lower().endswith('.pk3') and (d / f).is_file()]
            pk.sort(key=lambda s: str(d / s).upper())
            for f in pk:
                st = (d / f).stat()
                stamp.append((str(d / f), st.st_size, st.st_mtime_ns))
                paks.append(d / f)
        key = hashlib.sha256(json.dumps([game, [str(d) for d in dirs], stamp]).encode()).hexdigest()[:24]
        cpath = Path(cache) / ('shaders-%s.pkl' % key) if cache else None
        if cpath is not None and cpath.exists():
            try:
                return pickle.loads(cpath.read_bytes())
            except Exception:
                pass
        # g_pakFiles: each pak's files prepended as read, so the last pak's last file first. The packs' listing and
        # shader texts are cached apart, keyed by the packs alone: the stage's generated shaders change every build
        # and missed the whole table's cache, and opening ~900 packs' directories was most of a castle build's 12 s
        pakfiles, paktexts = pak_index(paks, [s for s in stamp if s[0].lower().endswith('.pk3')], cache)
        first = {}
        for nm, pk, info in pakfiles:
            first.setdefault(nm, (pk, info))
        # shaderlist.txt: every one found, in place order (vfsGetFileCount / LoadScriptFile index)
        files = []

        def have(f):
            return any(f.lower() == g.lower() for g in files)
        lists = [sd / n for sd, names in loose for n in names if n.lower() == 'shaderlist.txt']
        lists += [(pk, info) for nm, pk, info in pakfiles if nm == 'shaders/shaderlist.txt']
        for src in lists:
            text = src.read_bytes() if isinstance(src, Path) else paktexts[src]
            for t, _ in tokens(text):
                if not t.lower().endswith('.shader'):
                    t += '.shader'
                if not have(t):
                    files.append(t)
        if not files:
            for sd, names in loose:
                for n in names:
                    if n.lower().endswith('.shader') and not have(n):
                        files.append(n)
            for nm, pk, info in pakfiles:
                if nm.endswith('.shader') and nm.rsplit('/', 1)[0] == 'shaders':
                    f = nm.rsplit('/', 1)[1]
                    if not have(f):
                        files.append(f)
        table = ShaderTable()
        for f in files:
            text = None
            for sd, names in loose:
                hit = [n for n in names if n.lower() == f.lower()]
                if hit:
                    text = (sd / hit[0]).read_bytes()
                    break
            if text is None:
                hit = first.get(('shaders/' + f).lower())
                if hit is None:
                    continue
                text = paktexts[hit]
            table.parse(text)
        if cpath is not None:
            cpath.parent.mkdir(parents=True, exist_ok=True)
            tmp = cpath.with_suffix('.tmp')
            tmp.write_bytes(pickle.dumps(table))
            tmp.replace(cpath)
        return table


def pak_index(paks, stamp, cache=None):
    """ShaderTable.from_basepaths' view of the packs: [(name lower case, pack, member)] in g_pakFiles order (each
    pack's files prepended, so reversed) and {(pack, member): bytes} for every shaders/*.shader and
    shaders/shaderlist.txt. cache: a folder; keyed by the packs' paths, sizes and times."""
    key = hashlib.sha256(json.dumps(['pak_index', 1, stamp]).encode()).hexdigest()[:24]
    cpath = Path(cache) / ('paks-%s.pkl' % key) if cache else None
    if cpath is not None and cpath.exists():
        try:
            return pickle.loads(cpath.read_bytes())
        except Exception:
            pass
    pakfiles, texts = [], {}
    for pk in paks:
        try:
            z = zipfile.ZipFile(pk)
        except (zipfile.BadZipFile, OSError):
            continue
        with z:
            for info in z.infolist():
                if len(info.filename) < 256:
                    nm = info.filename.replace('\\', '/').lower()
                    pakfiles.append((nm, str(pk), info.filename))
                    if nm.startswith('shaders/') and (nm.endswith('.shader') or nm == 'shaders/shaderlist.txt'):
                        texts[(str(pk), info.filename)] = z.read(info)
    pakfiles.reverse()
    out = (pakfiles, texts)
    if cpath is not None:
        cpath.parent.mkdir(parents=True, exist_ok=True)
        tmp = cpath.with_suffix('.tmp')
        tmp.write_bytes(pickle.dumps(out))
        tmp.replace(cpath)
    return out


# ---------------------------------------------------------------- the plane table (map.cpp, -exactplanes)

class Planes:
    """mapplanes and FindFloatPlane's table with -exactplanes: rows (nx, ny, nz, d) as float values; `last` maps a
    plane's value (-0 == +0, as C compares) to the newest plane of that value. FindFloatPlane is last[value] when the
    points' test passes, else create(value). The rule as q3map2 runs it, one lookup at a time; tile() numbers a
    tile's lookups with number_planes, the same numbers and rows at once (checked against this)."""
    __slots__ = ('rows', 'last')

    def __init__(self):
        self.rows = []
        self.last = {}

    def create(self, n0, n1, n2, d):
        """CreateNewFloatPlane: the plane and its flipped twin, -0 stored as +0, an axial plane facing positive first"""
        if n0 == 0 and n1 == 0 and n2 == 0:
            return -1                   # (its length test: a normal here is a unit vector or zero)
        p = (n0 + 0.0, n1 + 0.0, n2 + 0.0, d + 0.0)
        q = (0.0 - n0, 0.0 - n1, 0.0 - n2, 0.0 - d)
        rows, last = self.rows, self.last
        base = len(rows)
        if ((n0 == 1 or n0 == -1 or n1 == 1 or n1 == -1 or n2 == 1 or n2 == -1)
                and (p[0] < 0 or p[1] < 0 or p[2] < 0)):
            rows += (q, p)
            last[q] = base
            last[p] = base + 1
            return base + 1
        rows += (p, q)
        last[p] = base
        last[q] = base + 1
        return base


def snap_normal(n0, n1, n2):
    """SnapNormal (SNAP_NORMAL_FIX) on a float normal: (n0, n1, n2, adjusted)"""
    adj = False
    if (n0 != 0 or n1 != 0) and abs(n0) < F_00025 and abs(n1) < F_00025:
        n0 = n1 = 0.0
        adj = True
    elif (n0 != 0 or n2 != 0) and abs(n0) < F_00025 and abs(n2) < F_00025:
        n0 = n2 = 0.0
        adj = True
    elif (n2 != 0 or n1 != 0) and abs(n2) < F_00025 and abs(n1) < F_00025:
        n2 = n1 = 0.0
        adj = True
    if n0 != 0 and -NORMAL_EPSILON < n0 < NORMAL_EPSILON:
        n0 = 0.0
        adj = True
    if n1 != 0 and -NORMAL_EPSILON < n1 < NORMAL_EPSILON:
        n1 = 0.0
        adj = True
    if n2 != 0 and -NORMAL_EPSILON < n2 < NORMAL_EPSILON:
        n2 = 0.0
        adj = True
    if adj:
        n0, n1, n2, _ = normalize_f(n0, n1, n2)
    return n0, n1, n2, adj


def normalize_f(x, y, z):
    """VectorNormalize on a float vector: in double, the result rounded to float; (x, y, z, float length)"""
    ln = math.sqrt(x * x + y * y + z * z)
    if ln == 0:
        return 0.0, 0.0, 0.0, 0.0
    return _F4.unpack(_F4.pack(x / ln, y / ln, z / ln, ln))


def snap_plane(n0, n1, n2, d, points):
    """FindFloatPlane(Plane3f, points)'s SnapPlaneImproved and point test, on values: (the snapped plane as a key,
    whether the points lie within distanceEpsilon of it). n and d float values, points doubles."""
    n0, n1, n2, adj = snap_normal(n0, n1, n2)
    if points:
        c0 = c1 = c2 = 0.0
        for p in points:
            c0 += p[0]
            c1 += p[1]
            c2 += p[2]
        k = float(len(points))
        c0, c1, c2 = c0 / k, c1 / k, c2 / k
        d = f32(n0 * c0 + n1 * c1 + n2 * c2)
    if (n0 == 0) + (n1 == 0) + (n2 == 0) > 1:
        r = float(round(d))
        e = f32(d - r)
        if -DISTANCE_EPSILON < e < DISTANCE_EPSILON:
            d = r
    ok = True
    for p in points:
        e = abs(p[0] * n0 + p[1] * n1 + p[2] * n2 - d)
        if not (e == 0 or e < DISTANCE_EPSILON):
            ok = False
            break
    return (n0, n1, n2, d), ok


def side_planes(pts):
    """BinRawBrush's planes for every side at once (exact elementwise IEEE): the grid points, PlaneFromPoints in
    double (dn, dd), the float plane snapped (fn, fd) and whether the points pass FindFloatPlane's test of it."""
    p = np.rint(pts.reshape(-1, 3, 3) * POINT_GRID) / POINT_GRID
    p0, p1, p2 = p[:, 0], p[:, 1], p[:, 2]
    a, b = p2 - p0, p1 - p0
    c = np.stack([a[:, 1] * b[:, 2] - a[:, 2] * b[:, 1],
                  a[:, 2] * b[:, 0] - a[:, 0] * b[:, 2],
                  a[:, 0] * b[:, 1] - a[:, 1] * b[:, 0]], axis=1)
    ln = np.sqrt(c[:, 0] * c[:, 0] + c[:, 1] * c[:, 1] + c[:, 2] * c[:, 2])
    zero = ln == 0
    with np.errstate(invalid='ignore', divide='ignore'):
        dn = c / ln[:, None]
    dn[zero] = 0.0
    dd = p0[:, 0] * dn[:, 0] + p0[:, 1] * dn[:, 1] + p0[:, 2] * dn[:, 2]
    dd[zero] = 0.0
    fn = dn.astype(np.float32)
    # SnapNormal
    T = np.float32(0.00025)
    af = np.abs(fn)
    nz = fn != 0
    adj = np.zeros(len(fn), bool)
    pairs = ((0, 1), (0, 2), (2, 1))
    taken = np.zeros(len(fn), bool)
    for i, j in pairs:
        m = ~taken & (nz[:, i] | nz[:, j]) & (af[:, i] < T) & (af[:, j] < T)
        fn[m, i] = 0
        fn[m, j] = 0
        taken |= m
    adj |= taken
    f64 = fn.astype(np.float64)
    small = (fn != 0) & (-NORMAL_EPSILON < f64) & (f64 < NORMAL_EPSILON)
    fn[small] = 0
    adj |= small.any(axis=1)
    if adj.any():
        v = fn[adj].astype(np.float64)
        l2 = np.sqrt(v[:, 0] * v[:, 0] + v[:, 1] * v[:, 1] + v[:, 2] * v[:, 2])
        with np.errstate(invalid='ignore', divide='ignore'):
            w = (v / l2[:, None]).astype(np.float32)
        w[l2 == 0] = 0
        fn[adj] = w
    # the distance through the points' centre (either branch with points), in double, stored as float
    cen = (((0.0 + p0) + p1) + p2) / 3.0
    f64 = fn.astype(np.float64)
    fd = (f64[:, 0] * cen[:, 0] + f64[:, 1] * cen[:, 1] + f64[:, 2] * cen[:, 2]).astype(np.float32)
    onaxis = (fn == 0).sum(axis=1) > 1
    r = np.rint(fd)
    e = (fd - r).astype(np.float64)
    snap = onaxis & (-DISTANCE_EPSILON < e) & (e < DISTANCE_EPSILON)
    fd[snap] = r[snap]
    fdd = fd.astype(np.float64)
    ok = np.ones(len(fn), bool)
    for k in range(3):
        q = p[:, k]
        e = np.abs(q[:, 0] * f64[:, 0] + q[:, 1] * f64[:, 1] + q[:, 2] * f64[:, 2] - fdd)
        ok &= (e == 0) | (e < DISTANCE_EPSILON)
    return dn, dd, fn, fd, ok


# ---------------------------------------------------------------- windings (polylib.cpp, brush.cpp)

def chop(w, n0, n1, n2, pd):
    """ChopWindingInPlaceAccu(w, plane, 0): the part of w on the plane's front"""
    dists = []
    front = back = False
    sides = []
    for p in w:
        d = p[0] * n0 + p[1] * n1 + p[2] * n2 - pd
        dists.append(d)
        if d > CHOP_EPSILON:
            sides.append(1)
            front = True
        elif d < -CHOP_EPSILON:
            sides.append(-1)
            back = True
        else:
            sides.append(0)
    if not front:
        return []
    if not back:
        return w
    m = len(w)
    sides.append(sides[0])
    dists.append(dists[0])
    f = []
    for i in range(m):
        p1 = w[i]
        s = sides[i]
        if s >= 0:
            f.append(p1)
            if s == 0:
                continue
        s2 = sides[i + 1]
        if s2 == 0 or s2 == s:
            continue
        p2 = w[i + 1] if i + 1 < m else w[0]
        t = dists[i] / (dists[i] - dists[i + 1])
        mid = (pd if n0 == 1 else -pd if n0 == -1 else p1[0] + t * (p2[0] - p1[0]),
               pd if n1 == 1 else -pd if n1 == -1 else p1[1] + t * (p2[1] - p1[1]),
               pd if n2 == 1 else -pd if n2 == -1 else p1[2] + t * (p2[2] - p1[2]))
        f.append(mid)
    if len(f) > MAX_POINTS_ON_WINDING:
        raise RuntimeError('ChopWindingInPlaceAccu: MAX_POINTS_ON_WINDING')
    return f


def _weld1(a, b):
    ai, bi = float(round(a)), float(round(b))
    ad, bd = abs(ai - a), abs(bi - b)
    if ad < bd:
        return ai if ad < SNAP_EPSILON else a
    return bi if bd < SNAP_EPSILON else b


def fix_winding(w):
    """FixWindingAccu: welds points closer than DEGENERATE_EPSILON, from the start again after each"""
    w = list(w)
    while len(w) > 1:
        done = True
        i = len(w) - 1
        for j in range(len(w)):
            a, b = w[i], w[j]
            x, y, z = a[0] - b[0], a[1] - b[1], a[2] - b[2]
            if math.sqrt(x * x + y * y + z * z) < DEGENERATE_EPSILON:
                w[i] = (_weld1(a[0], b[0]), _weld1(a[1], b[1]), _weld1(a[2], b[2]))
                del w[j]
                done = False
                break
            i = j
        if done:
            break
    return w


def base_winding_bounded(n0, n1, n2, d, mins, maxs):
    """BaseWindingForPlaneAccu(plane, minmax)"""
    n = (n0, n1, n2)
    z = 1 if abs(n1) > abs(n0) else 0
    z = 2 if abs(n2) > abs(n[z]) else z
    x = 0 if z == 2 else z + 1
    y = 0 if x == 2 else x + 1
    if abs(n[z]) < 0.56:
        raise RuntimeError('BaseWindingForPlaneAccu: no dominant axis found because normal is too short')
    out = []
    for ux, uy in ((maxs[x], maxs[y]), (maxs[x], mins[y]), (mins[x], mins[y]), (mins[x], maxs[y])):
        v = [0.0, 0.0, 0.0]
        v[x], v[y] = ux, uy
        v[z] = (d - ux * n[x] - uy * n[y]) / n[z]
        out.append(tuple(v))
    return out if n[z] > 0 else out[::-1]


def base_winding_huge(n0, n1, n2, d):
    """BaseWindingForPlaneAccu(plane): a square of half-size 262,144 x |vright| on the plane"""
    mx = f32(0.56)
    x = -1
    for i, c in enumerate((n0, n1, n2)):
        v = f32(abs(c))
        if v > mx:
            x, mx = i, v
    if x == -1:
        raise RuntimeError('BaseWindingForPlaneAccu: no dominant axis found because normal is too short')
    if x < 2:
        r0, r1, r2 = -n1, n0, 0.0
    else:
        r0, r1, r2 = 0.0, -n2, n1
    s = MAX_WORLD * 4.0
    r0, r1, r2 = r0 * s, r1 * s, r2 * s
    u0, u1, u2 = n1 * r2 - n2 * r1, n2 * r0 - n0 * r2, n0 * r1 - n1 * r0
    o0, o1, o2 = n0 * d, n1 * d, n2 * d
    return [((o0 - r0) + u0, (o1 - r1) + u1, (o2 - r2) + u2),
            ((o0 + r0) + u0, (o1 + r1) + u1, (o2 + r2) + u2),
            ((o0 + r0) - u0, (o1 + r1) - u1, (o2 + r2) - u2),
            ((o0 - r0) - u0, (o1 - r1) - u1, (o2 - r2) - u2)]


def _det3(m00, m01, m02, m10, m11, m12, m20, m21, m22):
    return m00 * (m11 * m22 - m21 * m12) - m01 * (m10 * m22 - m20 * m12) + m02 * (m10 * m21 - m11 * m20)


def brush_bounds_from_planes(P):
    """brushMinMaxFromPlanes: the bounds of every three planes' point inside all planes (DoubleMinMax)"""
    mins = [DBL_MAX, DBL_MAX, DBL_MAX]
    maxs = [-DBL_MAX, -DBL_MAX, -DBL_MAX]
    for a, b, c in combinations(range(len(P)), 3):
        A, B, C = P[a], P[b], P[c]
        det = _det3(A[0], A[1], A[2], B[0], B[1], B[2], C[0], C[1], C[2])
        if abs(det) < 1e-9:
            continue
        dx = _det3(A[3], A[1], A[2], B[3], B[1], B[2], C[3], C[1], C[2])
        dy = _det3(A[0], A[3], A[2], B[0], B[3], B[2], C[0], C[3], C[2])
        dz = _det3(A[0], A[1], A[3], B[0], B[1], B[3], C[0], C[1], C[3])
        v = (dx / det, dy / det, dz / det)
        good = True
        for q in P:
            if not (v[0] * q[0] + v[1] * q[1] + v[2] * q[2] - q[3] < ON_EPSILON):
                good = False
                break
        if good:
            for k in range(3):
                if v[k] < mins[k]:
                    mins[k] = v[k]
                if v[k] > maxs[k]:
                    maxs[k] = v[k]
    return mins, maxs


def brush_bounds_np(P):
    """brush_bounds_from_planes for a larger brush: the same arithmetic per triple, vectorised over the (j, k) of each
    i in the loops' order; a point is dropped at the first plane it is outside of (the test is all of them), and the
    points inside every plane extend the bounds in the loops' order"""
    P = np.asarray(P, np.float64)
    n = len(P)
    mins = [DBL_MAX, DBL_MAX, DBL_MAX]
    maxs = [-DBL_MAX, -DBL_MAX, -DBL_MAX]
    for i in range(n - 2):
        jj, kk = np.triu_indices(n - i - 1, k=1)
        A, B, C = P[i], P[jj + i + 1], P[kk + i + 1]

        def det(i0, i1, i2):
            return (A[i0] * (B[:, i1] * C[:, i2] - C[:, i1] * B[:, i2])
                    - A[i1] * (B[:, i0] * C[:, i2] - C[:, i0] * B[:, i2])
                    + A[i2] * (B[:, i0] * C[:, i1] - B[:, i1] * C[:, i0]))
        dt = det(0, 1, 2)
        keep = np.nonzero(~(np.abs(dt) < 1e-9))[0]
        if not len(keep):
            continue
        B, C, dt = B[keep], C[keep], dt[keep]
        v = np.stack([det(3, 1, 2), det(0, 3, 2), det(0, 1, 3)], axis=1) / dt[:, None]
        for q in P:
            e = v[:, 0] * q[0] + v[:, 1] * q[1] + v[:, 2] * q[2] - q[3]
            v = v[e < ON_EPSILON]
            if not len(v):
                break
        for q in v.tolist():
            for k in range(3):
                if q[k] < mins[k]:
                    mins[k] = q[k]
                if q[k] > maxs[k]:
                    maxs[k] = q[k]
    return mins, maxs


def create_windings(sides):
    """CreateBrushWindings (OFFSET_WINDING_CREATION) and BoundBrush: each side's float winding (side[4]) and the
    float bounds, or None when the brush encloses no valid volume"""
    P = [s[2] for s in sides]
    mins, maxs = brush_bounds_from_planes(P) if len(P) <= 9 else brush_bounds_np(P)
    if not (mins[0] < maxs[0] and mins[1] < maxs[1] and mins[2] < maxs[2]):
        return None
    off = ((mins[0] + maxs[0]) * 0.5, (mins[1] + maxs[1]) * 0.5, (mins[2] + maxs[2]) * 0.5)
    lo = (mins[0] - off[0], mins[1] - off[1], mins[2] - off[2])
    hi = (maxs[0] - off[0], maxs[1] - off[1], maxs[2] - off[2])
    no0, no1, no2 = -off[0], -off[1], -off[2]
    clip = []
    for q in P:
        m0, m1, m2 = -q[0], -q[1], -q[2]                       # flipped, then translated by -offset
        clip.append((m0, m1, m2, -q[3] + (m0 * no0 + m1 * no1 + m2 * no2)))
    fmin = [FLT_MAX, FLT_MAX, FLT_MAX]
    fmax = [-FLT_MAX, -FLT_MAX, -FLT_MAX]
    for i, s in enumerate(sides):
        c = clip[i]
        w = base_winding_bounded(-c[0], -c[1], -c[2], -c[3], lo, hi)
        pn = s[0]
        for j, cs in enumerate(sides):
            if not w:
                break
            if i == j or cs[0] == (pn ^ 1):
                continue
            q = clip[j]
            w = chop(w, q[0], q[1], q[2], q[3])
        w = fix_winding(w)
        if len(w) >= 3:
            o0, o1, o2 = off
            fw = [f32x3(p[0] + o0, p[1] + o1, p[2] + o2) for p in w]
            for p in fw:
                for k in range(3):
                    if p[k] < fmin[k]:
                        fmin[k] = p[k]
                    if p[k] > fmax[k]:
                        fmax[k] = p[k]
            s[4] = fw
        else:
            s[4] = []
    if not (fmin[0] < fmax[0] and fmin[1] < fmax[1] and fmin[2] < fmax[2]):
        return None
    if not all(-MAX_WORLD <= fmin[k] and fmax[k] <= MAX_WORLD for k in range(3)):
        return None
    return fmin, fmax


def anchored_windings(sides, anchor):
    """AnchoredBrushWindings: each side's double winding in the frame moved by -anchor; [] for a bevel"""
    a0, a1, a2 = -anchor[0], -anchor[1], -anchor[2]
    clip = []
    for s in sides:
        q = s[2] if s[2] is not None else s[7]
        t = q[3] + (q[0] * a0 + q[1] * a1 + q[2] * a2)          # translated by -anchor, then flipped
        clip.append((-q[0], -q[1], -q[2], -t))
    out = []
    for i, s in enumerate(sides):
        if s[3]:
            out.append([])
            continue
        c = clip[i]
        w = base_winding_huge(-c[0], -c[1], -c[2], -c[3])
        pn = s[0]
        for j, cs in enumerate(sides):
            if not w:
                break
            if i == j or cs[3] or cs[0] == (pn ^ 1):
                continue
            q = clip[j]
            w = chop(w, q[0], q[1], q[2], q[3])
        w = fix_winding(w)
        out.append(w if len(w) >= 3 else [])
    return out


def _near(a, b, eps):
    """|f32(a - b)| < eps for float values a, b: the double difference decides unless within a part in 1e6 of eps
    (rounding it to float moves it by at most a part in 2^24)"""
    e = abs(a - b)
    if e < eps * 0.999999:
        return True
    if e > eps * 1.000001:
        return False
    return abs(f32(a - b)) < eps


def _plane_equal(p, n0, n1, n2, d):
    """PlaneEqual(mapplane, Plane3f) with the epsilons (a pure test: the normal compared first)"""
    return ((p[0] == n0 or _near(p[0], n0, NORMAL_EPSILON))
            and (p[1] == n1 or _near(p[1], n1, NORMAL_EPSILON))
            and (p[2] == n2 or _near(p[2], n2, NORMAL_EPSILON))
            and (p[3] == d or _near(p[3], d, DISTANCE_EPSILON)))


def add_bevels(sides, fmin, bevels):
    """AddBrushBevels with -exactplanes on plane values only: each new bevel's snapped plane and FindFloatPlane's
    point test go to `bevels` in the order the planes are looked up, the side's ref naming it. sides: [local id,
    shader or None, double plane or None, bevel, float winding, surface flags, content flags, plane value, ref]"""
    anchor = tuple(float(math.floor((fmin[c] + 0.123456789) / 64)) * 64.0 for c in range(3))
    win = anchored_windings(sides, anchor)
    dmin = [1e30, 1e30, 1e30]
    dmax = [-1e30, -1e30, -1e30]
    for w in win:
        for v in w:
            for c in range(3):
                x = v[c] + anchor[c]
                if x < dmin[c]:
                    dmin[c] = x
                if dmax[c] < x:
                    dmax[c] = x
    any_flags = any(s[5] & BEVELS_MASK for s in sides)
    order = 0
    for axis in range(3):
        for dirn in (-1, 1):
            i = 0
            while i < len(sides):
                if sides[i][7][axis] == dirn:
                    break
                i += 1
            if i == len(sides):
                if len(sides) == MAX_BUILD_SIDES:
                    raise RuntimeError('MAX_BUILD_SIDES')
                n = [0.0, 0.0, 0.0]
                n[axis] = float(dirn)
                d = f32(dmax[axis]) if dirn == 1 else f32(-dmin[axis])
                key, ok = snap_plane(n[0], n[1], n[2], d, ())
                s = [None, None, None, True, [], 0, sides[0][6], key, -1 - len(bevels)]
                bevels.append((key, ok))
                sides.append(s)
                surf = 0
                if any_flags:
                    for o in sides:
                        for pt in o[4]:
                            if abs(f32(d - pt[axis])) < F_01:
                                surf |= o[5] & BEVELS_MASK
                                break
                s[5] = surf
            if i != order:
                sides[order], sides[i] = sides[i], sides[order]
            order += 1
    if len(sides) == 6:
        return
    # edge bevels (AddEdgeBevelsAnchored)
    win = anchored_windings(sides, anchor)
    count = len(sides)
    a0, a1, a2 = anchor
    for i in range(6, count):
        w = win[i]
        nw = len(w)
        si5 = sides[i][5] & BEVELS_MASK
        for j in range(nw):
            p, q = w[j], w[(j + 1) % nw]
            dv0, dv1, dv2 = p[0] - q[0], p[1] - q[1], p[2] - q[2]
            ln = math.sqrt(dv0 * dv0 + dv1 * dv1 + dv2 * dv2)
            if ln < 0.5:
                continue
            sc = 1.0 / ln
            v0, v1, v2, _ = snap_normal(*f32x3(dv0 * sc, dv1 * sc, dv2 * sc))
            if (v0 == -1 or v0 == 1 or (v0 == 0 and v1 == 0) or v1 == -1 or v1 == 1 or (v1 == 0 and v2 == 0)
                    or v2 == -1 or v2 == 1 or (v2 == 0 and v0 == 0)):
                continue
            p0, p1, p2 = p
            for axis in range(3):
                for dirn in (-1.0, 1.0):
                    # vector3_cross(vec, vec2) in float: with vec2 an axis each product and difference is exact
                    if axis == 0:
                        c0, c1, c2 = v1 * 0.0 - v2 * 0.0, v2 * dirn - v0 * 0.0, v0 * 0.0 - v1 * dirn
                    elif axis == 1:
                        c0, c1, c2 = v1 * 0.0 - v2 * dirn, v2 * 0.0 - v0 * 0.0, v0 * dirn - v1 * 0.0
                    else:
                        c0, c1, c2 = v1 * dirn - v2 * 0.0, v2 * 0.0 - v0 * dirn, v0 * 0.0 - v1 * 0.0
                    c0, c1, c2, ln2 = normalize_f(c0, c1, c2)
                    if ln2 < 0.5:
                        continue
                    ld = f32(p0 * c0 + p1 * c1 + p2 * c2)
                    pd = f32(ld + (a0 * c0 + a1 * c1 + a2 * c2))
                    hit = False
                    for k, o in enumerate(sides):
                        rk = o[7]
                        x = rk[0] - c0          # (|x| this far: PlaneEqual is false)
                        if -5.0001e-5 < x < 5.0001e-5 and _plane_equal(rk, c0, c1, c2, pd):
                            if o[3]:
                                o[5] |= si5
                            hit = True
                            break
                        if k >= count:
                            continue
                        wk = win[k]
                        if not wk:
                            continue
                        mb = 0.0
                        front = False
                        for pt in wk:
                            dd = c0 * pt[0] + c1 * pt[1] + c2 * pt[2] - ld
                            if dd > 0.1:
                                front = True
                                break
                            if dd < mb:
                                mb = dd
                        if front or mb > -0.1:
                            hit = True
                            break
                    if hit:
                        continue
                    if len(sides) == MAX_BUILD_SIDES:
                        raise RuntimeError('MAX_BUILD_SIDES')
                    at = (p0 + a0, p1 + a1, p2 + a2)
                    key, ok = snap_plane(c0, c1, c2, pd, (at,))
                    sides.append([None, None, None, True, [], si5, sides[0][6], key, -1 - len(bevels)])
                    bevels.append((key, ok))


# ---------------------------------------------------------------- a tile

PLAYERCLIP = S_CONT_PLAYERCLIP


def brush_contents(sides, infos):
    """SetBrushContents: (the content shader's record index, compile flags)"""
    si0 = infos[sides[0][1]]
    content, comp = si0.content, si0.compile
    cs, csi = si0, sides[0][1]
    for s in sides[1:]:
        si = infos[s[1]]
        content |= si.content
        comp |= si.compile
        if cs.compile & C_LIQUID:
            continue
        elif si.compile & C_LIQUID:
            cs, csi = si, s[1]
        elif cs.compile & C_FOG:
            continue
        elif si.compile & C_FOG:
            cs, csi = si, s[1]
        elif cs.content & PLAYERCLIP:
            continue
        elif si.content & PLAYERCLIP:
            cs, csi = si, s[1]
        elif not (cs.compile & C_SOLID):
            continue
        elif not (si.compile & C_SOLID):
            cs, csi = si, s[1]
    if (comp & C_DETAIL) and (comp & C_STRUCTURAL):
        comp &= ~C_DETAIL
    if (comp & C_TRANSLUCENT) and not (comp & C_STRUCTURAL):
        comp |= C_DETAIL
    return csi, comp


class Emitter:
    """EmitShader's table (writebsp.cpp)"""

    def __init__(self, table):
        self.table = table
        self.rows = []              # (name, surface, content)
        self.exact = {}             # (lower name, surface, content) -> first index
        self.by_surf = {}           # (lower name, surface) -> first index

    def emit(self, name, content, surface):
        if name is None:
            name = 'noshader'
        low = name.lower()
        if content is not None:
            i = self.exact.get((low, surface, content))
        else:
            i = self.by_surf.get((low, surface)) if surface is not None else None
            if surface is None:
                i = next((k for k, r in enumerate(self.rows) if r[0].lower() == low), None)
        if i is not None:
            return i
        si = self.table.lookup(name)
        i = len(self.rows)
        row = (si.shader, si.surface if surface is None else surface, si.content if content is None else content)
        self.rows.append(row)
        self.exact.setdefault((row[0].lower(), row[1], row[2]), i)
        self.by_surf.setdefault((row[0].lower(), row[1]), i)
        if si.damage:
            self.emit(si.damage, None, None)
        return i

    def array(self):
        a = np.zeros(len(self.rows), SHADER)
        for k, (n, s, c) in enumerate(self.rows):
            a[k]['name'] = n.encode('latin-1')[:63]
            a[k]['surface_flags'] = s
            a[k]['content_flags'] = c
        return a


def flip_key(k):
    return (-k[0] + 0.0, -k[1] + 0.0, -k[2] + 0.0, -k[3] + 0.0)


def brush_geometry(raw, infos):
    """One brush's steps after its sides' planes, on plane values only (no plane table): raw [(plane value, point
    test passed, double plane, record shader)] in record order. None when the brush is dropped, else (content
    shader's record index, opaque, bevel planes [(value, test passed)] in lookup order, sides [(ref, record shader or
    None, surface flags)]: ref >= 0 a raw side, -1 - k bevel k).

    RemoveDuplicateBrushPlanes compares plane numbers. Within a brush they follow from the values alone: a side
    whose test passes takes the newest plane of its value, one whose test fails makes a new pair, and a value's
    newest plane and its flipped value's newest plane are always twins (each new pair sets both), so local pair ids
    give the same equalities and twins as the table's numbers."""
    local = {}
    nid = 0
    sides = []
    for ref, (key, ok, dp, sh) in enumerate(raw):
        if key[0] == 0 and key[1] == 0 and key[2] == 0:
            continue                    # (CreateNewFloatPlane: a bad normal, planenum -1, removed)
        lid = local.get(key) if ok else None
        if lid is None:
            lid = nid
            nid += 2
            local[key] = lid
            local[flip_key(key)] = lid + 1
        si = infos[sh]
        sides.append([lid, sh, dp, False, [], si.surface, si.content, key, ref])
    i = 0
    while i < len(sides):
        j = i + 1
        while j < len(sides):
            if sides[i][0] == sides[j][0]:
                del sides[j]
            elif sides[i][0] == (sides[j][0] ^ 1):
                return None             # (a mirrored plane: the brush is invalid)
            else:
                j += 1
        i += 1
    if not sides:
        return None
    csi, comp = brush_contents(sides, infos)
    b = create_windings(sides)
    if b is None:
        return None
    if comp & C_ORIGIN:
        return None
    if comp & C_AREAPORTAL:
        raise RuntimeError('-brushbin: an areaportal brush')
    bevels = []
    add_bevels(sides, b[0], bevels)
    return csi, not (comp & C_TRANSLUCENT), bevels, [(s[8], s[1], s[5]) for s in sides]


def _keys(fn, fd):
    """the snapped planes as value tuples"""
    return list(map(tuple, np.concatenate([fn.astype(np.float64), fd.astype(np.float64)[:, None]], 1).tolist()))


def _geometry_job(job):
    """a batch of brushes' geometry (in a worker process): numpy slices in, the batch's brush_geometry results as
    arrays out: per brush kept, content shader, opaque, bevel and side counts; per bevel its plane value and test;
    per side its ref (>= 0 a record side of the brush, -1 - k its bevel k), record shader (-1: a bevel) and surface
    flags"""
    ns, fn, fd, ok, dn, dd, sh, infos = job
    keys = _keys(fn, fd)
    dps = list(map(tuple, np.concatenate([dn, dd[:, None]], 1).tolist()))
    raw = list(zip(keys, ok.tolist(), dps, sh.tolist()))
    nb = len(ns)
    kept = np.zeros(nb, bool)
    csi = np.zeros(nb, np.int32)
    opaque = np.zeros(nb, bool)
    nbev = np.zeros(nb, np.int32)
    nside = np.zeros(nb, np.int32)
    bkeys, boks, refs, shs, surfs = [], [], [], [], []
    first = 0
    for b, n in enumerate(ns):
        g = brush_geometry(raw[first:first + n], infos)
        first += n
        if g is None:
            continue
        kept[b] = True
        csi[b], opaque[b] = g[0], g[1]
        nbev[b] = len(g[2])
        nside[b] = len(g[3])
        for key, bok in g[2]:
            bkeys.append(key)
            boks.append(bok)
        for ref, s, surf in g[3]:
            refs.append(ref)
            shs.append(-1 if s is None else s)
            surfs.append(surf)
    return (kept, csi, opaque, nbev, nside, np.array(bkeys, np.float64).reshape(-1, 4), np.array(boks, bool),
            np.array(refs, np.int64), np.array(shs, np.int32), np.array(surfs, np.int64))


def number_planes(keys, oks):
    """FindFloatPlane's plane numbers for a sequence of lookups (keys (n, 4) float32 snapped plane values in lookup
    order, oks whether each one's points passed the test), and the plane table they make: (numbers, rows).

    The table's rule (Planes): a lookup whose test passes takes the newest plane of its value, else (or when there
    is none) a new pair is made, the value and its flipped twin, rows 2k and 2k + 1, an axial plane facing positive
    first, -0 stored as +0. A value and its flip share a class; a class's pairs are only made by its lookups, so a
    lookup's plane is in the newest pair its class made at or before it."""
    keys = np.asarray(keys, np.float32) + np.float32(0.0)
    n = len(keys)
    if not n:
        return np.zeros(0, np.int64), np.zeros((0, 4), np.float32)
    nrm = keys[:, :3]
    zero = (nrm == 0).all(axis=1)                       # (CreateNewFloatPlane: a bad normal, -1, no pair)
    # the class: the orientation whose first non-zero normal component is positive
    lead = np.where(nrm[:, 0] != 0, nrm[:, 0], np.where(nrm[:, 1] != 0, nrm[:, 1], nrm[:, 2]))
    orient = lead < 0                                   # (the key is the class value's flip)
    canon = np.where(orient[:, None], -keys, keys) + np.float32(0.0)
    v = np.ascontiguousarray(canon).view(np.uint64).reshape(n, 2)
    order = np.lexsort((v[:, 1], v[:, 0]))              # (stable: a class's lookups stay in lookup order)
    vs = v[order]
    newc = np.ones(n, bool)
    newc[1:] = (vs[1:] != vs[:-1]).any(axis=1)
    cls_sorted = np.cumsum(newc) - 1
    first = np.zeros(n, bool)
    first[order] = newc
    create = ~zero & (~np.asarray(oks, bool) | first)
    if not create.any():
        return np.full(n, -1, np.int64), np.zeros((0, 4), np.float32)
    rank = np.cumsum(create) - 1                        # (the pair a creating lookup makes)
    # the newest pair of the class at or before each lookup: a running maximum within each class's run
    m = np.int64(n + 1)
    val = cls_sorted.astype(np.int64) * m + np.where(create[order], rank[order], -1)
    acc = np.maximum.accumulate(val) - cls_sorted.astype(np.int64) * m
    pair = np.empty(n, np.int64)
    pair[order] = acc
    # the pair's first row: the creating key, or its flip for an axial plane facing negative
    axial = (np.abs(nrm) == 1).any(axis=1)
    swap = axial & (nrm < 0).any(axis=1)
    row0_flip = (orient ^ swap)[create]                 # (row 2k holds the class value's flip)
    num = 2 * pair + (orient != row0_flip[pair])
    num[zero] = -1
    c = canon[create]
    f = -c + np.float32(0.0)
    rows = np.empty((2 * len(c), 4), np.float32)
    rows[0::2] = np.where(row0_flip[:, None], f, c)
    rows[1::2] = np.where(row0_flip[:, None], c, f)
    return num, rows


def tile(ns, shader, pts, infos, table, pool=None, batch=4000):
    """One tile's records through -brushbin: (shaders, planes, brushes, sides, (read, kept)) as colcook reads them.
    ns: sides per brush; shader: per side an index into infos (ShaderInfo per record shader name); pts: (sides, 9).
    The brushes' geometry runs in `pool` (a concurrent.futures executor) when given; then the plane numbers in the
    order q3map2 looks them up (a brush's sides, then its bevels) and EmitBrushes' order and shaders."""
    ns_a = np.asarray(ns, np.int64)
    ns = ns_a.tolist()
    nb = len(ns)
    if any(n >= MAX_BUILD_SIDES for n in ns):
        raise RuntimeError('MAX_BUILD_SIDES')
    dn, dd, fn, fd, ok = side_planes(np.asarray(pts, np.float64))
    shader = np.asarray(shader)
    # the geometry, in batches of about equal cost (a brush's steps grow about with its sides squared)
    starts = np.concatenate([[0], np.cumsum(ns_a)])
    cost = [40 + n * n for n in ns]
    limit = max(sum(cost) // (64 if pool is not None else 1), 1)
    bounds = []
    b0 = 0
    while b0 < nb:
        b1, c = b0, 0
        while b1 < nb and (b1 == b0 or (c + cost[b1] <= limit and b1 - b0 < batch)):
            c += cost[b1]
            b1 += 1
        bounds.append((b0, b1))
        b0 = b1

    def job(b0, b1):
        s0, s1 = starts[b0], starts[b1]
        return (ns[b0:b1], fn[s0:s1], fd[s0:s1], ok[s0:s1], dn[s0:s1], dd[s0:s1], shader[s0:s1], infos)
    if pool is not None and len(bounds) > 1:
        parts = list(pool.map(_geometry_job, [job(b0, b1) for b0, b1 in bounds]))
    else:
        parts = [_geometry_job(job(b0, b1)) for b0, b1 in bounds]
    if parts:
        kept, csi, opaque, nbev, nside, bkeys, boks, refs, shs, surfs = (np.concatenate(x) for x in zip(*parts))
    else:
        kept, opaque = np.zeros(0, bool), np.zeros(0, bool)
        csi, nbev, nside = (np.zeros(0, np.int32) for _ in range(3))
        bkeys, boks = np.zeros((0, 4)), np.zeros(0, bool)
        refs, shs, surfs = np.zeros(0, np.int64), np.zeros(0, np.int32), np.zeros(0, np.int64)
    del parts
    # the lookups in q3map2's order: brush b's record sides, then its bevels
    nbev = nbev.astype(np.int64)
    bev_start = np.concatenate([[0], np.cumsum(nbev)])
    brush_of_side = np.repeat(np.arange(nb), ns_a)
    pos_raw = np.arange(len(fn)) + bev_start[brush_of_side]
    brush_of_bev = np.repeat(np.arange(nb), nbev)
    pos_bev = starts[brush_of_bev + 1] + np.arange(len(bkeys))
    E = len(fn) + len(bkeys)
    ek = np.empty((E, 4), np.float32)
    eok = np.empty(E, bool)
    ek[pos_raw, :3], ek[pos_raw, 3], eok[pos_raw] = fn, fd, ok
    ek[pos_bev], eok[pos_bev] = bkeys.astype(np.float32), boks
    num, rows = number_planes(ek, eok)
    del ek, eok
    # each kept brush's sides' plane numbers
    kb = np.nonzero(kept)[0]
    nside = nside.astype(np.int64)
    brush_of_fside = np.repeat(np.arange(nb), nside)
    raw = refs >= 0
    epos = np.where(raw, starts[brush_of_fside] + np.maximum(refs, 0) + bev_start[brush_of_fside],
                    starts[brush_of_fside + 1] + bev_start[brush_of_fside] + np.where(raw, 0, -1 - refs))
    spn = num[epos]
    # EmitBrushes' order: the opaque brushes reversed, then the translucent in order
    out_b = np.concatenate([kb[opaque[kb]][::-1], kb[~opaque[kb]]])
    side_start = np.concatenate([[0], np.cumsum(nside)])
    cnt = nside[out_b]
    first_out = np.concatenate([[0], np.cumsum(cnt)])[:-1]
    gidx = np.repeat(side_start[out_b], cnt) + (np.arange(int(cnt.sum())) - np.repeat(first_out, cnt))
    # EmitShader's table: each record shader and bevel surface flags emitted in the order they first appear (a later
    # emit of the same gives the index the first gave)
    R = len(infos)
    bsurf, binv = np.unique(surfs[shs < 0], return_inverse=True)
    stok = shs.astype(np.int64)
    stok[shs < 0] = R + binv
    ntok = 1 + cnt
    seq_first = np.concatenate([[0], np.cumsum(ntok)])[:-1]
    seq = np.empty(int(ntok.sum()), np.int64)
    seq[seq_first] = csi[out_b]
    seq_side = np.repeat(seq_first + 1, cnt) + (np.arange(int(cnt.sum())) - np.repeat(first_out, cnt))
    seq[seq_side] = stok[gidx]
    em = Emitter(table)
    tok_row = np.zeros(R + len(bsurf), np.int64)
    if len(seq):
        u, fi = np.unique(seq, return_index=True)
        for t in u[np.argsort(fi)].tolist():
            if t < R:
                si = infos[t]
                tok_row[t] = em.emit(si.shader, si.content, si.surface)
            else:
                tok_row[t] = em.emit(None, None, int(bsurf[t - R]))
    br = np.zeros(len(out_b), BRUSH)
    br['first_side'] = first_out
    br['num_sides'] = cnt
    br['shader'] = tok_row[csi[out_b]]
    sd = np.zeros(len(gidx), SIDE)
    sd['plane'] = spn[gidx]
    sd['shader'] = tok_row[stok[gidx]]
    pl = np.zeros(len(rows), PLANE)
    pl['normal'] = rows[:, :3]
    pl['dist'] = rows[:, 3]
    return em.array(), pl, br, sd, (nb, len(kb))


def cook_tiles(r, groups, table, jobs=None):
    """colcook's per-tile outputs for records r (colcook.Recs) in tiles `groups`, as q3map2 -brushbin gives them.
    jobs: worker processes for the brushes' geometry (default: the CPUs less two; 1: none)"""
    infos = [table.lookup('textures/' + n) for n in r.names]
    first = np.concatenate([[0], np.cumsum(r.ns.astype(np.int64))])
    jobs = max(1, min(16, (os.cpu_count() or 2) - 2)) if jobs is None else jobs
    pool = None
    if jobs > 1 and len(r.ns) > 8000:
        from concurrent.futures import ProcessPoolExecutor
        pool = ProcessPoolExecutor(max_workers=jobs)
    out = []
    try:
        for g in groups:
            g = np.asarray(g, np.int64)
            cnt = r.ns[g].astype(np.int64)
            start = np.cumsum(cnt) - cnt
            idx = np.repeat(first[g], cnt) + (np.arange(int(cnt.sum())) - np.repeat(start, cnt))
            out.append(tile(r.ns[g], r.shader[idx], r.pts[idx], infos, table, pool))
    finally:
        if pool is not None:
            pool.shutdown()
    return out


# ---------------------------------------------------------------- the gate

def compare_tile(ref, got):
    """The first differences between two tiles' (shaders, planes, brushes, sides, counts): a report dict"""
    names = ('shaders', 'planes', 'brushes', 'sides')
    rep = {'identical': True}
    for nm, a, b in zip(names, ref[:4], got[:4]):
        same = len(a) == len(b) and a.tobytes() == b.tobytes()
        rep[nm] = {'ref': len(a), 'python': len(b), 'identical': same}
        if not same:
            rep['identical'] = False
            m = min(len(a), len(b))
            va = np.ascontiguousarray(a[:m]).view(np.uint8).reshape(m, -1)
            vb = np.ascontiguousarray(b[:m]).view(np.uint8).reshape(m, -1)
            diff = np.nonzero((va != vb).any(axis=1))[0] if m else np.zeros(0, np.int64)
            if not len(diff) and len(a) != len(b):
                diff = np.array([m])
            rep[nm]['differ'] = int((va != vb).any(axis=1).sum()) if m else 0
            rep[nm]['first'] = [int(k) for k in diff[:8]]
            rep[nm]['examples'] = [{'index': int(k), 'ref': _row(a[k]) if k < len(a) else None,
                                    'python': _row(b[k]) if k < len(b) else None} for k in diff[:4]]
    rep['counts'] = {'ref': [int(x) for x in ref[4]], 'python': [int(x) for x in got[4]]}
    if tuple(ref[4]) != tuple(got[4]):
        rep['identical'] = False
    return rep


def _row(x):
    out = {}
    for k in x.dtype.names:
        v = x[k]
        out[k] = v.decode('latin-1').rstrip('\0') if isinstance(v, bytes) else np.asarray(v).tolist()
    return out


def side_detail(ref, got, k):
    """for a side index k differing: the brush it is in, on both sides"""
    def where(t):
        br = t[2]
        b = int(np.searchsorted(br['first_side'], k, side='right') - 1)
        return {'brush': b, 'brush_row': _row(br[b]) if 0 <= b < len(br) else None,
                'side_in_brush': int(k - br['first_side'][b]) if 0 <= b < len(br) else None,
                'plane': _row(t[1][t[3][k]['plane']]) if k < len(t[3]) and 0 <= t[3][k]['plane'] < len(t[1]) else None}
    return {'ref': where(ref), 'python': where(got)}


def save_dump(path, r, groups, basepaths, ref=None):
    """colcook's inputs (and q3map2's per-tile outputs, when given) for gate reruns"""
    d = {'ns': r.ns, 'shader': r.shader, 'pts': r.pts, 'names': np.array(json.dumps(r.names)),
         'groups': np.array(json.dumps([np.asarray(g).tolist() for g in groups])),
         'basepaths': np.array(json.dumps([str(b) for b in basepaths]))}
    for k, t in enumerate(ref or []):
        for nm, a in zip(('sh', 'pl', 'br', 'sd'), t[:4]):
            d['ref%d_%s' % (k, nm)] = a
        d['ref%d_counts' % k] = np.array(t[4])
    np.savez(path, **d)


def load_dump(path):
    import colcook
    z = np.load(path)
    r = colcook.Recs(z['ns'], z['shader'], z['pts'], json.loads(str(z['names'])))
    groups = [np.array(g, np.int64) for g in json.loads(str(z['groups']))]
    ref = []
    k = 0
    while 'ref%d_sh' % k in z:
        ref.append(tuple(z['ref%d_%s' % (k, nm)] for nm in ('sh', 'pl', 'br', 'sd')) + (tuple(z['ref%d_counts' % k]),))
        k += 1
    return r, groups, json.loads(str(z['basepaths'])), ref


def gate(ref_tiles, got_tiles):
    """per tile compare_tile, plus totals"""
    tiles = []
    for k, (a, b) in enumerate(zip(ref_tiles, got_tiles)):
        rep = compare_tile(a, b)
        if not rep['identical'] and not rep['sides']['identical'] and rep['sides'].get('first'):
            rep['sides']['detail'] = side_detail(a, b, rep['sides']['first'][0])
        tiles.append(rep)
    tot = {nm: sum(t[nm]['ref'] for t in tiles) for nm in ('shaders', 'planes', 'brushes', 'sides')}
    return {'identical': all(t['identical'] for t in tiles) and len(ref_tiles) == len(got_tiles),
            'compared': tot, 'tiles': tiles}


def collision_glb(src, dst):
    """A copy of a levelport glb with only its collision: entity, patch and render nodes marked "ignore" (a gameplay
    glb such as w3build.py's as a --col-chunk test input: a cooked chunk is brushes only). The binary chunk is kept."""
    raw = Path(src).read_bytes()
    if raw[:4] != b'glTF':
        raise SystemExit('%s: not a .glb' % src)
    jlen = struct.unpack_from('<I', raw, 12)[0]
    doc = json.loads(raw[20:20 + jlen])
    rest = raw[20 + jlen:]
    n = 0
    for nd in doc.get('nodes', []):
        ex = nd.get('extras')
        role = ex.get('lp') if isinstance(ex, dict) else None
        if role in ('entity', 'patch') or (role in (None, 'render') and 'mesh' in nd):
            nd['extras'] = dict(ex) if isinstance(ex, dict) else {}
            nd['extras']['lp'] = 'ignore'
            n += 1
    js = json.dumps(doc, separators=(',', ':')).encode('utf-8')
    js += b' ' * (-len(js) % 4)
    body = struct.pack('<II', len(js), 0x4E4F534A) + js + rest
    Path(dst).parent.mkdir(parents=True, exist_ok=True)
    Path(dst).write_bytes(b'glTF' + struct.pack('<II', 2, 12 + len(body)) + body)
    return n


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('collision-glb', help='a copy of a glb with its entity, patch and render nodes ignored')
    c.add_argument('src')
    c.add_argument('dst')
    g = sub.add_parser('gate', help='a colcook dump (gltf2map --cook-gate) through Python against its q3map2 tiles')
    g.add_argument('dump')
    g.add_argument('--q3map2', help='run this -brushbin q3map2 for the reference instead of the dump\'s tiles')
    g.add_argument('--out', help='the report (default: <dump>.gate.json)')
    a = ap.parse_args(argv)
    if a.cmd == 'collision-glb':
        print('%s: %d nodes ignored -> %s' % (a.src, collision_glb(a.src, a.dst), a.dst))
        return 0
    r, groups, basepaths, ref = load_dump(a.dump)
    t0 = time.time()
    table = ShaderTable.from_basepaths(basepaths, cache=default_cache())
    t1 = time.time()
    got = cook_tiles(r, groups, table)
    t2 = time.time()
    if a.q3map2:
        import colcook
        ref = colcook.brushbin_tiles(r, groups, [a.q3map2, '-bsp', '-game', 'ja'] + sum(
            (['-fs_basepath', str(b)] for b in basepaths), []) + ['-meta', '-exactplanes', '-pointgrid', '16384',
                                                                  'chunk.map'], None, str(Path(a.q3map2).parent))[0]
    rep = gate(ref, got)
    rep['seconds'] = {'shaders': round(t1 - t0, 2), 'python': round(t2 - t1, 2)}
    out = Path(a.out or (str(a.dump) + '.gate.json'))
    out.write_text(json.dumps(rep, indent=1), encoding='utf-8')
    print('gate: %s  %s  python %.1f s (shaders %.1f s) -> %s' % (
        'IDENTICAL' if rep['identical'] else 'DIFFERENT', rep['compared'], t2 - t1, t1 - t0, out))
    return 0 if rep['identical'] else 1


def default_cache():
    try:
        import paths
        return paths.LEVELPORT / 'cache' / 'colbrushes'
    except Exception:
        return None


if __name__ == '__main__':
    sys.exit(main())
