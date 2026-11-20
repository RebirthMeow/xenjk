"""pack_texture_dedupe.py -- one file per byte-identical texture group in a character pack.

    python -I tools/characters/pack_texture_dedupe.py <in.pk3> --out <out.pk3> [--base GameData/base] [--report r.json]
    python -I tools/characters/pack_texture_dedupe.py <in.pk3> [...] --out-dir <folder> [--base ...] [--report r.json]

A converted character pack often stores one image under several names (Zayne: one ORM map under 9 names, the skin as
both `skin` and `aerithbridalskin`); the renderer loads and streams each name on its own. This writes a copy of the
pack in which every drawn reference to a duplicate reads one canonical file of its group, and drops the duplicates
nothing names any more. Members keep their order, compression (stored stays stored: DDS ranges stream) and times.
Read-only on the input; nothing under --base is written.

The renderer's rules it follows (rd-vulkan tr_shader.cpp ParseStage / FinishShader, vk_local.h textureMapTypes):
- A lit stage (rgbGen lightingDiffuse[Entity]) with an albedo X and no explicit physical map takes the first of
  X_rmo, _rmos, _moxr, _mosr, _orm, _orms that exists; with no explicit normal map, the first of X_n, X_nh. An explicit
  keyword (ormMap and so on, normalMap) loads with the same flags and sets the same type, and the name search is
  skipped. A shader with no stanza is implicit: its own name is the albedo, searched the same way.
- Groups: texture entries under models/players/ with equal CRC-32 and size, then compared byte for byte; the
  canonical name is the shortest (then first) of the group.
Rewrites, in this pack's .shader/.mtr stanzas only (stanza names, .skin files and GLMs are never changed):
1. A path token naming a duplicate D becomes its canonical C when every companion the renderer would search for D
   matches C's (both absent, or both present and byte-identical).
2. In a lit stage whose albedo X has no explicit physical (normal) map and whose searched companion is a duplicate F of
   C: an explicit keyword for F's kind naming C is added after the albedo line.
A duplicate's file is dropped only when nothing can read it any more: no token in this pack's texts, no GLM surface
name, no .skin/.shader/.mtr/.glm of another installed pack, and no shader that still searches for it by name (an
implicit shader, or a stanza stage without that explicit kind, whose albedo it is a companion of).
Proof (always run; exit 1 on a failure): every shader the pack's skins and GLMs draw is resolved before and after
(stage by stage: the albedo and the physical and normal maps as the renderer would load them, or the stage's other
explicit maps), and the CRC-32 of every image read must be the same in the same roles.
"""
import argparse
import json
import os
import re
import sys
import zipfile
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from packio import rewrite  # noqa: E402

TEX_EXT = ('.dds', '.png', '.tga', '.jpg', '.jpeg')
PHYS = (('_rmo', 'rmoMap'), ('_rmos', 'rmosMap'), ('_moxr', 'moxrMap'), ('_mosr', 'mosrMap'), ('_orm', 'ormMap'),
        ('_orms', 'ormsMap'))
NORM = (('_n', 'normalMap'), ('_nh', 'normalHeightMap'))
SUFFIXES = tuple(s for s, _ in PHYS + NORM)
PHYS_KEYS = {k.lower() for _, k in PHYS} | {'specmap', 'specularmap', 'physicalmap'}
NORM_KEYS = {k.lower() for _, k in NORM}
KEY_ROLE = {k.lower(): 'phys' + s for s, k in PHYS}
KEY_ROLE.update({k.lower(): 'norm' + s for s, k in NORM})
ALBEDO_KEYS = ('map', 'clampmap', 'diffusemap')
PATH = re.compile(r'(?i)models/players/[^\s"\x00,{}]+')
LIT = re.compile(r'(?i)rgbgen\s+lightingdiffuse')


def base(n):
    b, e = os.path.splitext(n)
    return b.lower() if e.lower() in TEX_EXT else n.lower()


def stanzas(text):
    """top-level shader blocks: [(name, body_start, body_end)]"""
    out, t, end = [], re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), text), 0
    for m in re.finditer(r'(?m)^[ \t]*(\S+)[ \t]*\r?\n?[ \t]*\{', t):
        # a match inside the previous stanza's body is a stage ("}" + newline + "{" between two stages reads as a
        # name and a brace): skipping it keeps the stanzas top-level
        if m.start() < end or '{' in m.group(1) or '}' in m.group(1):
            continue
        depth, k = 1, m.end()
        while k < len(t) and depth:
            depth += {'{': 1, '}': -1}.get(t[k], 0)
            k += 1
        out.append((m.group(1).lower(), m.end(), k - 1))
        end = k
    return out


def stages(body):
    """depth-1 blocks of a stanza body: [(start, end)] of the stage text"""
    return [(m.start(1), m.end(1)) for m in re.finditer(r'\{([^{}]*)\}', re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), body))]


def stage_info(st):
    toks = [(k.lower(), base(p)) for k, p in re.findall(r'(?i)(\S+)\s+(models/players/[^\s"]+)', st)]
    albedo = next((p for k, p in toks if k in ALBEDO_KEYS), None)
    keys = {k for k, _ in toks}
    return toks, albedo, bool(LIT.search(st)), bool(keys & PHYS_KEYS), bool(keys & NORM_KEYS)


def search(x, tex, table):
    for s, k in table:
        if x + s in tex:
            return s, k, x + s
    return None


def resolve(name, tex, stz):
    """[(role, base path)] a drawn shader reads"""
    out = []
    if name not in stz:
        out.append(('albedo', name))
        for table, kind in ((PHYS, 'phys'), (NORM, 'norm')):
            f = search(name, tex, table)
            if f:
                out.append((kind + f[0], f[2]))
        return out
    body = stz[name]
    for s0, s1 in stages(body):
        toks, albedo, lit, ephys, enorm = stage_info(body[s0:s1])
        for k, p in toks:
            out.append((KEY_ROLE.get(k, 'albedo' if k in ALBEDO_KEYS else k), p))
        if albedo and lit:
            if not ephys:
                f = search(albedo, tex, PHYS)
                if f:
                    out.append(('phys' + f[0], f[2]))
            if not enorm:
                f = search(albedo, tex, NORM)
                if f:
                    out.append(('norm' + f[0], f[2]))
    return out


def word(b):
    return re.compile(re.escape(b) + r'(?=[.\s"\x00,}]|$)')


def scan_install(base_dir):
    """every installed pack's shader texts (they read textures by path) and drawn names (skins and GLMs draw shaders,
    e.g. a menu names pack's alias skins): {pack name: (shader texts, drawn names)}"""
    out = {}
    for p in sorted(os.listdir(base_dir)):
        if not p.lower().endswith('.pk3'):
            continue
        try:
            oz = zipfile.ZipFile(os.path.join(base_dir, p))
        except Exception:
            continue
        shaders, drawn = [], set()
        for i in oz.infolist():
            n = i.filename.lower()
            if n.endswith(('.shader', '.mtr')):
                shaders.append(oz.read(i).decode('latin-1').lower())
            elif n.endswith('.skin'):
                for line in oz.read(i).decode('latin-1').splitlines():
                    if ',' in line:
                        drawn.add(base(line.split(',', 1)[1].strip()))
            elif n.endswith('.glm'):
                drawn.update(base(x) for x in PATH.findall(oz.read(i).decode('latin-1')))
        out[p.lower()] = (shaders, drawn)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inp', nargs='+', help='packs to deduplicate')
    ap.add_argument('--out', help='output pack (one input)')
    ap.add_argument('--out-dir', help='output folder (any number of inputs), each pack under its own name')
    ap.add_argument('--base', help='installed packs, for references from other packs')
    ap.add_argument('--report', help='JSON report (all inputs)')
    a = ap.parse_args()
    if bool(a.out) == bool(a.out_dir) or (a.out and len(a.inp) > 1):
        sys.exit('give --out for one pack or --out-dir')
    install = scan_install(a.base) if a.base else {}
    reps, bad = [], 0
    for inp in a.inp:
        me = os.path.basename(inp).lower()
        others = [v for k, v in install.items() if k != me]
        out = a.out or os.path.join(a.out_dir, os.path.basename(inp))
        r = dedupe(inp, out, others)
        reps.append(r)
        bad += bool(r['proof_failures'])
    if a.report:
        with open(a.report, 'w') as f:
            json.dump(reps, f, indent=1)
    if len(reps) > 1:
        print('total: %d packs, %.1f -> %.1f MB, %d with proof failures' % (
            len(reps), sum(r['in_bytes'] for r in reps) / 2**20, sum(r['out_bytes'] for r in reps) / 2**20, bad))
    sys.exit(1 if bad else 0)


def dedupe(inp, out_path, others):
    """one pack; others: [(shader texts, drawn names)] of every other installed pack"""
    z = zipfile.ZipFile(inp)
    infos = z.infolist()
    tex, texts, glms = {}, {}, []
    exts = defaultdict(dict)   # base name -> {extension: entry}: every file a name can resolve to
    for i in infos:
        n = i.filename.lower()
        if n.startswith('models/players/') and n.endswith(TEX_EXT):
            tex.setdefault(base(n), i)
            exts[base(n)].setdefault(os.path.splitext(n)[1], i)
        elif n.endswith(('.skin', '.shader', '.mtr')):
            texts[i.filename] = z.read(i).decode('latin-1')
        elif n.endswith('.glm'):
            glms.append(z.read(i).decode('latin-1').lower())
    glm_blob = '\n'.join(glms)
    cache = {}

    def loaded(b):
        """the entry the renderer reads for name b: its .dds when there is one, else the image"""
        return exts[b].get('.dds', tex[b])

    def data(i):
        if i.filename not in cache:
            cache[i.filename] = z.read(i)
        return cache[i.filename]

    def same(x, y):
        """x and y are the same texture under every extension either has (so a .dds and its image fallback agree)"""
        if exts[x].keys() != exts[y].keys():
            return False
        return all(ix.CRC == iy.CRC and ix.file_size == iy.file_size and data(ix) == data(iy)
                   for ix, iy in ((exts[x][e], exts[y][e]) for e in exts[x]))

    groups = defaultdict(list)
    for b in tex:
        i = loaded(b)
        groups[(i.CRC, i.file_size)].append(b)
    canon = {}
    for names in groups.values():
        if len(names) < 2:
            continue
        names.sort(key=lambda n: (len(n), n))
        for d in names[1:]:
            if same(d, names[0]):
                canon[d] = names[0]

    def companions_match(d, c):
        for s in SUFFIXES:
            if (d + s in tex) != (c + s in tex) or (d + s in tex and not same(d + s, c + s)):
                return False
        return True
    redirect = {d: c for d, c in canon.items() if companions_match(d, c)}

    stz = {}
    for fn, t in texts.items():
        if fn.lower().endswith(('.shader', '.mtr')):
            for nm, s0, s1 in stanzas(t):
                stz[nm] = t[s0:s1]

    # rewrite the stanzas
    tok = re.compile(r'(?i)models/players/[^\s"\x00,{}]+')

    def path_of(b, like):
        """b with the referencing token's extension style: none when the token had none"""
        ext = os.path.splitext(like)[1]
        return b + (os.path.splitext(tex[b].filename)[1].lower() if ext.lower() in TEX_EXT else '')

    new_texts, added = dict(texts), 0
    for fn, t in texts.items():
        if not fn.lower().endswith(('.shader', '.mtr')):
            continue
        out, last = [], 0
        for nm, s0, s1 in stanzas(t):
            body = t[s0:s1]
            nb, blast = [], 0
            for g0, g1 in stages(body):
                st = body[g0:g1]
                st = tok.sub(lambda m: path_of(redirect[base(m.group(0))], m.group(0)) if base(m.group(0)) in redirect else m.group(0), st)
                toks, albedo, lit, ephys, enorm = stage_info(st)
                if albedo and lit:
                    ins = []
                    for table, has in ((PHYS, ephys), (NORM, enorm)):
                        if has:
                            continue
                        f = search(albedo, tex, table)
                        if f and f[2] in canon:
                            ins.append('%s %s' % (f[1], path_of(canon[f[2]], tex[canon[f[2]]].filename)))
                    if ins:
                        m = re.search(r'(?im)^([ \t]*)(map|clampmap|diffusemap)\s+models/players/[^\r\n]*(\r?\n)', st)
                        if m:
                            indent, eol = m.group(1), m.group(3)
                            st = st[:m.end()] + ''.join(indent + x + eol for x in ins) + st[m.end():]
                            added += len(ins)
                nb.append(body[blast:g0])
                nb.append(st)
                blast = g1
            nb.append(body[blast:])
            out.append(t[last:s0])
            out.append(''.join(nb))
            last = s1
        out.append(t[last:])
        new_texts[fn] = ''.join(out)

    stz_after = {}
    for fn, t in new_texts.items():
        if fn.lower().endswith(('.shader', '.mtr')):
            for nm, s0, s1 in stanzas(t):
                stz_after[nm] = t[s0:s1]

    # other packs' references (scan_install)
    other_blob = '\n'.join(t for shaders, _ in others for t in shaders)
    other_drawn = set().union(*[d for _, d in others]) if others else set()

    # who still searches by name: implicit shaders drawn (skins, GLMs, other packs) and stanza stages without the kind
    searched = {'phys': set(), 'norm': set()}
    drawn = set()
    for t in [v for k, v in new_texts.items() if k.lower().endswith('.skin')]:
        for line in t.splitlines():
            if ',' in line:
                v = base(line.split(',', 1)[1].strip())
                if v and v != '*off':
                    drawn.add(v)
    drawn.update(base(x) for x in PATH.findall(glm_blob))
    drawn.update(n for n in other_drawn if n.startswith('models/players/'))
    for n in drawn:
        if n not in stz_after:
            searched['phys'].add(n)
            searched['norm'].add(n)
    for nm, body in stz_after.items():
        for g0, g1 in stages(body):
            toks, albedo, lit, ephys, enorm = stage_info(body[g0:g1])
            if albedo and lit:
                if not ephys:
                    searched['phys'].add(albedo)
                if not enorm:
                    searched['norm'].add(albedo)
    # what this pack still reads by path: tokens in stanza bodies, and drawn names that have no stanza (implicit)
    reads = set()
    for body in stz_after.values():
        reads.update(base(x) for x in PATH.findall(body))
    reads.update(n for n in drawn if n not in stz_after)
    # only a texture some drawn shader read before the change may be dropped: anything else is found another way (the
    # picker's icon_<skin> images, the UI, cgame), which this tool does not model (icons were dropped once)
    read_before = set()
    for nm, body in stz.items():
        read_before.update(base(x) for x in PATH.findall(body))
    read_before.update(n for n in drawn if n not in stz)
    for x in list(read_before):
        for table in (PHYS, NORM):
            f = search(x, tex, table)
            if f:
                read_before.add(f[2])
    drop = set()
    for d in canon:
        if d not in read_before or os.path.basename(d).startswith('icon_'):
            continue
        if d in reads or word(d).search(other_blob):
            continue
        kept_by = [x for x in tex if any(d == x + s for s, _ in PHYS) and (x in searched['phys'] or word(x).search(other_blob))]
        kept_by += [x for x in tex if any(d == x + s for s, _ in NORM) and (x in searched['norm'] or word(x).search(other_blob))]
        if kept_by:
            continue
        drop.add(d)
    tex_after = {b: i for b, i in tex.items() if b not in drop}

    # proof
    fails, checked = [], 0
    names = set(drawn)
    for t in [v for k, v in texts.items() if k.lower().endswith('.skin')]:
        for line in t.splitlines():
            if ',' in line:
                v = base(line.split(',', 1)[1].strip())
                if v and v != '*off':
                    names.add(v)
    for n in sorted(names):
        if not n.startswith('models/players/'):
            continue
        rb = sorted((r, loaded(p).CRC if p in tex else None) for r, p in resolve(n, tex, stz))
        ra = sorted((r, loaded(p).CRC if p in tex_after else None) for r, p in resolve(n, tex_after, stz_after))
        checked += 1
        if rb != ra:
            fails.append({'shader': n, 'before': rb, 'after': ra})

    os.makedirs(os.path.dirname(os.path.abspath(out_path)) or '.', exist_ok=True)
    rewrite(z, out_path, replace={fn: t.encode('latin-1') for fn, t in new_texts.items()},
            skip=lambda fn: fn.lower().endswith(TEX_EXT) and base(fn) in drop)
    rep = {'in': inp, 'out': out_path, 'in_bytes': os.path.getsize(inp), 'out_bytes': os.path.getsize(out_path),
           'duplicates': len(canon), 'redirected_tokens_for': len(redirect), 'keywords_added': added,
           'dropped': sorted(drop), 'dropped_bytes': sum(i.file_size for d in drop for i in exts[d].values()),
           'kept_duplicates': sorted(set(canon) - drop), 'drawn_checked': checked, 'proof_failures': fails}
    print('%s: %d duplicates, %d dropped (%.1f MB), %d keywords added; %.1f -> %.1f MB; %d drawn shaders checked, '
          '%d failures' % (os.path.basename(inp), len(canon), len(drop), rep['dropped_bytes'] / 2**20, added,
                           rep['in_bytes'] / 2**20, rep['out_bytes'] / 2**20, checked, len(fails)), flush=True)
    return rep


if __name__ == '__main__':
    main()
