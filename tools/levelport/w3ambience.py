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
"""Beauclair's city ambience from the game's own sound placements and audio, as looping target_speakers in a pk3.

Inputs:
- the census of the city's sound components (summary.json: one row per CSoundAmbientAreaComponent,
  CSoundEmitterComponent and bard emitter in the box, with its events, game position, Q3 position and polygon);
- the game's audio: content/content0/soundspc.cache of the remaster (CS3W v2: u64 info offset at 16, u64 file count at
  24, u64 names offset at 32; 24-byte entries of name offset, data offset, size). It holds the Wwise banks (.bnk,
  bank version 150) and the streamed sources (<source id>.wem);
- the map's current entity string (the BSP's entity lump in the map pk3).

An event name resolves to its sources: FNV-1 32 of the lower-cased name is a CAkEvent (HIRC 4: u8 count, u32 action
ids); a play action (HIRC 3, type 0x0403) names a target at payload offset 6; a target's sounds are found by inverting
each node's parent id (a CAkSound's at payload offset 26, a container's at 12, when the node has no effects); a
CAkSound (HIRC 2) has its source id at 9. A source is in its bank's DIDX/DATA or is <id>.wem in the cache. The play
modes of the containers (random intervals, volumes, switches) are not decoded: EVENTS below sets how each is baked.

Each chosen event becomes one mono 22,050 Hz 16-bit WAV that loops seamlessly:
- 'loop': its sources one after another, joined and wrapped with short crossfades (crowd walla, ships, bards);
- 'dense': its short sources laid end to end with overlap on a circular buffer (granular water, fire, wind);
- 'sparse': its short sources placed at random gaps on a circular buffer (gulls, birds).
Events whose sources are the same share one WAV.
Placement: every exterior city row naming a chosen event; a point emitter at its position; an area on a grid of GRID_M
inside its polygon (its centre when no grid point falls inside), a walla area's points alternating its quad
events. Speakers of one event closer than MERGE_M are merged, each event keeps at most its cap (farthest-point
order), and the total is held to --max-speakers by priority.

The engine reads maps/<map>.ent in place of the BSP's entity lump (cm_load.cpp), so the map needs no rebuild:
the .ent is the current entity string, unchanged, followed by the speakers (target_speaker, spawnflags 1, "noise").
Grains stay baked into loops: cgame passes cg.time as a local set's last-wave time every frame
(cg_ents.c, S_AddLocalSet), so a localSet's timeBetweenWaves does not space its subwaves in multiplayer.

    python tools/levelport/w3ambience.py --summary <summary.json> --map-pk3 <zz_w3_city.pk3> --out <pack folder>
        [--map w3_city] [--max-speakers 120] [--budget-mb 150] [--game DIR] [--keep-stage]

--zones (the client's ambience player, cgame/cg_ambience.c) writes maps/<map>.amb in place of the .ent: every exterior
city area as a zone (its polygon and a height band, heard from its nearest point, all round you inside it), every point
emitter as a point, each event a set with its kind (a baked loop, one-shots at gaps, or the four-channel crowd murmur
of a walla area), reach (the component's maxDistance), gain, falloff curve and hours. One-shot events keep their clips
as separate WAVs. --rules takes wwise_rules.json (the containers' gaps, volumes, attenuation curves and hours decoded
from the banks); without it gaps come from EVENTS and hours from the names.

    python tools/levelport/w3ambience.py --zones [--rules wwise_rules.json] --summary <summary.json>
        --map-pk3 <zz_w3_city.pk3> --out <pack folder>

Writes <out>/stage/ (sound/w3_city/*.wav, maps/<map>.ent), <out>/zz_<map>_sound.pk3 and <out>/w3ambience.json (the
events, their sources, modes, durations, the speakers and the memory estimate: 16-bit at 44.1 kHz once loaded).
vgmstream-cli decodes the .wem sources (Wwise Opus/Vorbis); ffmpeg downmixes and resamples.
"""
import argparse
import collections
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import wave
import zipfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import q3bsp  # noqa: E402

RATE = 22050
LOADED_RATE = 44100                     # the budget's bound: 16-bit at s_khz 44 (the 64-bit client's soundlist
                                        # shows these files held at their own 22,050 Hz, half of it)
UPM = 37.5
GRID_M = 40.0                           # about the engine's reach (full volume 256 u, silent ~1,500 u)
MERGE_M = 20.0
XFADE_S = 0.25

# event -> (mode, cap on speakers, priority (lower first), seconds of loop, [gap min, gap max] for sparse)
EVENTS = {
    # crowds and walla
    'amb_dyn_bob_EXT_crowd_city_market_1_FL': ('loop', 4, 0, 60, None),
    'amb_dyn_bob_EXT_crowd_city_market_1_FR': ('loop', 4, 0, 60, None),
    'amb_dyn_bob_EXT_crowd_city_FL': ('loop', 6, 0, 60, None),
    'amb_dyn_bob_EXT_crowd_city_FR': ('loop', 6, 0, 60, None),
    'amb_dyn_bob_EXT_crowd_2_city_FL': ('loop', 6, 0, 60, None),
    'amb_dyn_bob_EXT_crowd_2_city_FR': ('loop', 6, 0, 60, None),
    'amb_dyn_bob_EXT_crowd_city_distr_upper_city_25m': ('loop', 12, 1, 60, None),
    'amb_dyn_bob_EXT_village_1_W_FL': ('loop', 3, 2, 45, None),
    'amb_dyn_bob_EXT_village_1_W_FR': ('loop', 3, 2, 45, None),
    # bards (always playing here: the game plays them while their musicians stand at their action points)
    'mus_em_bob_bard_group_ext': ('loop', 6, 0, 300, None),
    'mus_em_bob_bard_group_ext_far': ('loop', 2, 0, 300, None),
    # harbour
    'amb_dyn_bob_harbour_seagulls_2_EM_180m': ('sparse', 3, 0, 40, (1.5, 6.0)),
    'amb_dyn_bob_harbour_seagulls_1_RND': ('sparse', 4, 0, 40, (2.0, 8.0)),
    'amb_dyn_bob_harbour_ship_squeaks_EM_40m': ('loop', 3, 1, 60, None),
    'amb_dyn_bob_harbour_ship_sail_and_clatter_EM_40m': ('loop', 1, 1, 60, None),
    'amb_bob_city_water_canal_medium_EXT': ('dense', 6, 1, 30, None),
    'amb_bob_R_nml_g_water_lake_small_15m': ('dense', 4, 2, 30, None),
    'amb_g_EM_rope_creaks_1_quiet_15m': ('sparse', 4, 3, 30, (2.0, 7.0)),
    # water in the streets
    'amb_g_ext_EM_water_fountain_small_1_loop': ('dense', 10, 1, 30, None),
    'amb_g_water_STATIC_waterfall_medium_1_30m': ('dense', 4, 2, 30, None),
    'amb_g_water_STATIC_river_small_fast_1': ('dense', 3, 2, 30, None),
    # birds and insects (day)
    'amb_bob_g_EM_animal_bird_city_close_45_80m': ('sparse', 5, 2, 45, (3.0, 12.0)),
    'amb_bob_g_EM_animal_bird_city_close_1_80m': ('sparse', 3, 2, 45, (3.0, 12.0)),
    'amb_bob_g_EM_animal_bird_city_close_2_80m': ('sparse', 2, 2, 45, (3.0, 12.0)),
    'amb_g_ext_EM_animal_bird_tit_wings_1_often_RND': ('sparse', 3, 3, 40, (2.0, 9.0)),
    'amb_bob_g_EM_animal_insect_bee_close_01': ('sparse', 4, 3, 30, (1.0, 5.0)),
    # fire and wind
    'fx_fire_burning_bonfire_large_loop_15m': ('dense', 4, 2, 30, None),
    'fx_fire_burning_brazier_big_loop_15m': ('dense', 3, 3, 30, None),
    'amb_bob_g_EM_wind_whistling': ('sparse', 3, 3, 40, (4.0, 14.0)),
}
LOOP_MIN_S = 6.0          # a source this long or longer is a loop of its own
CANON = {e.lower(): e for e in EVENTS}    # placements name some events in other cases (the hash is case-blind)


# ---- the sound cache and its banks ----------------------------------------------------------------------------------

class Cache:
    def __init__(self, game):
        self.path = Path(game) / 'content' / 'content0' / 'soundspc.cache'
        with open(self.path, 'rb') as f:
            h = f.read(48)
            if h[:4] != b'CS3W':
                raise ValueError('%s: not a CS3W sound cache' % self.path)
            info, n, names = struct.unpack_from('<QQQ', h, 16)
            f.seek(names)
            nb = f.read(info - names)
            f.seek(info)
            ib = f.read(24 * n)
        self.files = {}
        for i in range(n):
            no, off, sz = struct.unpack_from('<QQQ', ib, 24 * i)
            self.files[nb[no:nb.index(b'\0', no)].decode('latin-1').lower()] = (off, sz)

    def read(self, name, sub=None):
        off, sz = self.files[name.lower()]
        if sub:
            off, sz = off + sub[0], sub[1]
        with open(self.path, 'rb') as f:
            f.seek(off)
            return f.read(sz)


def fnv1_32(name):
    h = 2166136261
    for c in name.lower().encode('latin-1'):
        h = (h * 16777619) & 0xFFFFFFFF
        h ^= c
    return h


class Banks:
    """every bank's HIRC nodes and DIDX entries; children by inverted parent ids"""
    CONTAINERS = (5, 6, 7, 9)

    def __init__(self, cache):
        self.cache = cache
        self.nodes, self.didx = {}, {}
        for b in sorted(k for k in cache.files if k.endswith('.bnk')):
            d = cache.read(b)
            ch, p = {}, 0
            while p + 8 <= len(d):
                tag, sz = struct.unpack_from('<4sI', d, p)
                ch[tag] = (p + 8, sz)
                p += 8 + sz
            if b'DIDX' in ch:
                p, sz = ch[b'DIDX']
                dp = ch[b'DATA'][0] if b'DATA' in ch else 0
                for i in range(0, sz, 12):
                    sid, off, s = struct.unpack_from('<III', d, p + i)
                    self.didx[(b, sid)] = (dp + off, s)
            if b'HIRC' in ch:
                p, _sz = ch[b'HIRC']
                q = p + 4
                for _ in range(struct.unpack_from('<I', d, p)[0]):
                    t, ln = struct.unpack_from('<BI', d, q)
                    pl = d[q + 5:q + 5 + ln]
                    q += 5 + ln
                    self.nodes.setdefault(struct.unpack_from('<I', pl, 0)[0], (t, b, pl))
        cids = {i for i, (t, _b, _p) in self.nodes.items() if t in self.CONTAINERS}
        self.children = collections.defaultdict(list)
        for i, (t, _b, pl) in self.nodes.items():
            if t in (2, 5, 6, 9):
                first, fx = (26, pl[19]) if t == 2 else (12, pl[5])
                par = None
                if fx == 0 and len(pl) >= first + 4 and struct.unpack_from('<I', pl, first)[0] in cids:
                    par = struct.unpack_from('<I', pl, first)[0]
                else:
                    for o in range(first, min(len(pl) - 3, first + 80)):
                        if struct.unpack_from('<I', pl, o)[0] in cids:
                            par = struct.unpack_from('<I', pl, o)[0]
                            break
                if par is not None:
                    self.children[par].append(i)

    def _sounds(self, node, seen):
        if node in seen or node not in self.nodes:
            return []
        seen.add(node)
        t, b, pl = self.nodes[node]
        if t == 2:
            plug, _st, sid = struct.unpack_from('<IBI', pl, 4)
            return [(b, plug, sid)]
        return [s for c in sorted(self.children.get(node, [])) for s in self._sounds(c, seen)]

    def sources(self, event):
        """the media of an event's play actions: [(source id, bytes)]"""
        eid = fnv1_32(event)
        if eid not in self.nodes:
            return []
        pl = self.nodes[eid][2]
        out, seen = [], set()
        for a in struct.unpack_from('<%dI' % pl[4], pl, 5):
            if a not in self.nodes:
                continue
            atype, target = struct.unpack_from('<HI', self.nodes[a][2], 4)
            if atype != 0x0403:
                continue
            for b, plug, sid in self._sounds(target, seen):
                if plug >> 16 == 0x65:                        # a synthesised source (silence, tone): no media
                    continue
                if (b, sid) in self.didx:
                    out.append((sid, self.cache.read(b, self.didx[(b, sid)])))
                elif '%d.wem' % sid in self.cache.files:
                    out.append((sid, self.cache.read('%d.wem' % sid)))
        return out


# ---- audio ----------------------------------------------------------------------------------------------------------

def tool(name, winget):
    p = shutil.which(name)
    if p:
        return p
    for c in Path(os.path.expandvars(r'%LOCALAPPDATA%\Microsoft\WinGet\Packages')).glob(winget):
        if c.is_file():
            return str(c)
    sys.exit('w3ambience: %s not found (PATH or winget)' % name)


def decode(wem, vgm, ffmpeg, tmp):
    """a .wem's samples as mono float32 at RATE"""
    src, wav = Path(tmp) / 'in.wem', Path(tmp) / 'in.wav'
    src.write_bytes(wem)
    subprocess.run([vgm, '-o', str(wav), str(src)], check=True, capture_output=True)
    raw = subprocess.run([ffmpeg, '-v', 'error', '-i', str(wav), '-ac', '1', '-ar', str(RATE), '-f', 's16le', '-'],
                         check=True, capture_output=True).stdout
    return np.frombuffer(raw, '<i2').astype(np.float32) / 32768.0


def fades(x, n):
    x = x.copy()
    n = min(n, len(x) // 2)
    if n:
        r = np.linspace(0, 1, n, dtype=np.float32)
        x[:n] *= r
        x[-n:] *= r[::-1]
    return x


def circular_add(buf, x, at):
    n = len(buf)
    for k in range(0, len(x), n):
        part = x[k:k + n]
        s = (at + k) % n
        e = min(n, s + len(part))
        buf[s:e] += part[:e - s]
        if e - s < len(part):
            buf[:len(part) - (e - s)] += part[e - s:]


def bake(clips, mode, seconds, gaps, rng):
    """one seamless loop from an event's clips"""
    xf = int(XFADE_S * RATE)
    if mode == 'loop':
        seconds = min(seconds, sum(len(c) for c in clips) / RATE)     # each clip once at most: a loop of its own
        seq, total = [], 0
        while total < seconds * RATE and len(seq) < 64:
            for c in clips:
                if seq and total + len(c) > seconds * RATE * 1.1:
                    total = seconds * RATE
                    break
                seq.append(c)
                total += len(c) - xf
                if total >= seconds * RATE:
                    break
            if all(len(c) < xf * 4 for c in clips):
                break
        n = int(max(total, xf * 4))
        buf, at = np.zeros(n, np.float32), 0
        for c in seq:
            circular_add(buf, fades(c, xf), at)
            at += len(c) - xf
        return buf
    n = int(seconds * RATE)
    buf = np.zeros(n, np.float32)
    at = 0
    order = list(rng.permutation(len(clips)))
    k = 0
    while at < n:
        c = clips[order[k % len(order)]]
        k += 1
        if k % len(order) == 0:
            order = list(rng.permutation(len(clips)))
        circular_add(buf, fades(c, int(0.02 * RATE)), at)
        if mode == 'dense':
            at += max(int(len(c) * 0.6), int(0.05 * RATE))
        else:
            at += int(rng.uniform(*gaps) * RATE)
    return buf


def write_wav(path, x):
    peak = float(np.abs(x).max()) if len(x) else 0.0
    if peak > 0.99:
        x = x * (0.99 / peak)
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes((x * 32767).astype('<i2').tobytes())


# ---- placement ------------------------------------------------------------------------------------------------------

def inside(pt, poly):
    x, y, c = pt[0], pt[1], False
    for i in range(len(poly)):
        (x0, y0), (x1, y1) = poly[i][:2], poly[i - 1][:2]
        if (y0 > y) != (y1 > y) and x < (x1 - x0) * (y - y0) / (y1 - y0) + x0:
            c = not c
    return c


def area_points(row):
    poly = row['polygon_q3']
    xs, ys = [p[0] for p in poly], [p[1] for p in poly]
    z = min(p[2] for p in poly) + 1.5 * UPM
    step = GRID_M * UPM
    pts = []
    for gx in np.arange(min(xs) + step / 2, max(xs), step):
        for gy in np.arange(min(ys) + step / 2, max(ys), step):
            if inside((gx, gy), poly):
                pts.append((float(gx), float(gy), z))
    return pts or [(sum(xs) / len(xs), sum(ys) / len(ys), z)]


def spread(pts, k):
    """k of pts in farthest-point order"""
    if len(pts) <= k:
        return pts
    a = np.array(pts)
    order = [0]
    while len(order) < k:
        d = np.min(np.linalg.norm(a[:, None] - a[order][None], axis=2), axis=1)
        order.append(int(np.argmax(d)))
    return [pts[i] for i in order]


def place(rows, max_speakers):
    cands = collections.defaultdict(list)
    for r in rows:
        if r['interior_guess'] or 'beauclair' not in r['layer'] or r['layer'].startswith(('quests/', 'sounds/quest')):
            continue
        evs = [CANON[e.lower()] for e in r['events'] if e.lower() in CANON]
        if not evs:
            continue
        if r['polygon_q3']:
            pts = area_points(r)
            if r['walla']:
                for i, p in enumerate(pts):
                    cands[evs[i % len(evs)]].append(p)
            else:
                for p in pts:
                    cands[evs[0]].append(p)
        else:
            p = r['q3_pos']
            cands[evs[0]].append((p[0], p[1], p[2] + 0.5 * UPM))
    chosen = {}
    for e, pts in cands.items():
        kept = []
        for p in pts:
            if all(math.dist(p, q) >= MERGE_M * UPM for q in kept):
                kept.append(p)
        chosen[e] = spread(kept, EVENTS[e][1])
    # the total held to max_speakers: drop the lowest priority, most numerous first
    while sum(len(v) for v in chosen.values()) > max_speakers:
        e = max((e for e in chosen if chosen[e]), key=lambda e: (EVENTS[e][2], len(chosen[e])))
        chosen[e] = chosen[e][:-1]
    return {e: v for e, v in chosen.items() if v}


# ---- entity string --------------------------------------------------------------------------------------------------

def entity_string(map_pk3, name):
    with zipfile.ZipFile(map_pk3) as z:
        data = z.read('maps/%s.bsp' % name)
    with tempfile.NamedTemporaryFile(suffix='.bsp', delete=False) as f:
        f.write(data)
    try:
        bsp = q3bsp.Bsp(f.name)
        n = len(bsp.entities)
    finally:
        Path(f.name).unlink()
    off, ln = struct.unpack_from('<ii', data, 8)
    return data[off:off + ln].rstrip(b'\0').decode('latin-1'), n


def wav_name(e):
    """a file name that keeps sound/w3_city/<name>.wav inside MAX_QPATH (64, with its terminator)"""
    n = e.lower()
    return n if len(n) <= 44 else '%s_%06x' % (n[:37], fnv1_32(e) & 0xFFFFFF)


def speaker(e, wav, p):
    return ('{\n"classname" "target_speaker"\n"spawnflags" "1"\n"noise" "sound/w3_city/%s.wav"\n'
            '"origin" "%.1f %.1f %.1f"\n"w3_event" "%s"\n}\n' % (wav, p[0], p[1], p[2], e))


# ---- zones: maps/<map>.amb for the client's ambience player -------------------------------------------------------

SKIP_WORDS = ('rain', '_tent_', 'roof', 'mus_loc', 'q70', 'corpse', 'cave', 'basement', 'storage', '_in_')
DENSE_WORDS = ('water', 'fire', 'fountain', 'waterfall', 'wind_forest', 'insect', 'cricket', 'grasshopper', 'frog')
NIGHT_WORDS = ('night', 'cricket', 'frog', 'owl')
DAY_WORDS = ('bird', 'seagull', 'bee_', 'bumblebee', 'grasshopper', 'crowd', 'market', 'village', 'tit_wings', 'vireo')
ZONE_BELOW_M, ZONE_ABOVE_M = 3.0, 15.0     # an area's height band round its polygon (the census has no height)
GRAIN_CLIPS = 24                           # clips kept for a one-shot event
DEFAULT_GAPS = (2.0, 8.0)                  # seconds between one-shots when neither the rules nor EVENTS give them


def name_hours(e):
    """day or night by the event's name (when the rules give no hours); (0, 0) always"""
    n = e.lower()
    if any(w in n for w in NIGHT_WORDS):
        return (20.5, 5.5)
    if any(w in n for w in DAY_WORDS):
        return (5.5, 20.5)
    return (0.0, 0.0)


def event_spec(e):
    return EVENTS.get(CANON.get(e.lower(), ''), (None, None, 4, None, None))


def zone_kind(e, clips, rule):
    """'loop' (long sources), 'dense' (short sources baked into a texture loop) or 'grains' (one-shots at gaps)"""
    if clips and min(len(c) for c in clips) >= LOOP_MIN_S * RATE:
        return 'loop'
    if rule.get('gap_ms'):
        return 'grains'
    mode = event_spec(e)[0]
    if mode in ('loop', 'dense'):
        return mode
    if mode == 'sparse':
        return 'grains'
    return 'dense' if any(w in e.lower() for w in DENSE_WORDS) else 'grains'


def band_site(bands, band, q3):
    """(index, site) of the band's site nearest q3, or None"""
    sites = (bands.get(band) or {}).get('sites') or []
    best = None
    for i, st in enumerate(sites):
        p = st.get('q3_pos')
        if p:
            d = math.dist(p[:2], q3[:2])
            if best is None or d < best[0]:
                best = (d, i, st)
    return (best[1], best[2]) if best else None


def db_gain(db):
    return 10.0 ** (db / 20.0)


def normalised(c):
    peak = float(np.abs(c).max()) if len(c) else 0.0
    return c * (0.99 / peak) if peak > 0.99 else c


def zones_main(a, rows):
    out = Path(a.out)
    stage = out / 'stage'
    if stage.exists():
        shutil.rmtree(stage)
    rules, bands = {}, {}
    if a.rules:
        rj = json.load(open(a.rules, encoding='utf-8'))
        ev = rj.get('events', rj)
        rules = {k.lower(): v for k, v in ev.items() if isinstance(v, dict)}
        bands = rj.get('bands', {}) or {}
    else:
        print('w3ambience: no --rules: gaps from EVENTS, hours from the names, the default falloff')
    # the exterior city rows by event; a walla area keeps its four events together
    inst = collections.defaultdict(list)      # set key -> [('zone'|'point', data)]
    reach_m = {}
    for r in rows:
        if r['interior_guess'] or 'beauclair' not in r['layer'] or r['layer'].startswith(('quests/', 'sounds/quest')):
            continue
        evs = [e for e in r['events'] if not e.lower().endswith('.bnk')]
        if r.get('music_band') and not evs:
            evs = ['mus_em_bob_bard_group_ext']
        evs = [e for e in evs if not any(w in e.lower() for w in SKIP_WORDS) and not e.lower().endswith('_int')]
        if not evs:
            continue
        m = r['max_distance_m'] or 40.0
        if r['polygon_q3']:
            poly = r['polygon_q3']
            zs = [q[2] for q in poly]
            data = ('zone', (min(zs) - ZONE_BELOW_M * UPM, max(zs) + ZONE_ABOVE_M * UPM,
                             [(q[0], q[1]) for q in poly]))
        else:
            data = ('point', tuple(r['q3_pos']))
        quad_tags = ('_FL', '_FR', '_RL', '_RR')
        if r['walla'] and len(evs) >= 4 and all(e[-3:].upper() in quad_tags for e in evs[:4]):
            key = ('walla', tuple(sorted(evs[:4], key=lambda e: quad_tags.index(e[-3:].upper()))))
        else:
            key = ('one', evs[0])
        if r.get('music_band'):
            site = band_site(bands, r['music_band'], r['q3_pos'])
            if site is not None and site[1].get('exterior') is False:
                continue                                   # an indoor band (its event is the interior one)
            key = key + (r['music_band'], site[0] if site else 0)
        elif evs[0].lower().startswith('mus_em_'):
            continue                                       # a bard emitter without its band's activator
        inst[key].append(data)
        reach_m[key] = max(reach_m.get(key, 0.0), m)
    vgm = tool('vgmstream-cli', 'vgmstream.vgmstream_*/vgmstream-cli.exe')
    ffmpeg = tool('ffmpeg', 'Gyan.FFmpeg_*/ffmpeg-*/bin/ffmpeg.exe')
    banks = Banks(Cache(a.game))
    rng = np.random.default_rng(7)
    sounds, snd_index = [], {}                 # [(path, ms)], wav name -> index
    sets, lines_inst, report = [], [], {}
    loaded = 0.0

    def add_sound(name, x):
        nonlocal loaded
        if name not in snd_index:
            write_wav(stage / 'sound' / a.map / ('%s.wav' % name), x)
            loaded += len(x) * 2                 # held at its own rate (the 64-bit client's soundlist)
            snd_index[name] = len(sounds)
            sounds.append(('sound/%s/%s.wav' % (a.map, name), int(len(x) * 1000 / RATE)))
        return snd_index[name]

    def prio(k):
        e = k[1][0] if k[0] == 'walla' else k[1]
        return (event_spec(e)[2], -len(inst[k]), e)

    clip_cache = {}
    with tempfile.TemporaryDirectory() as tmp:
        def clips_of(e):
            if e not in clip_cache:
                cl = [decode(w, vgm, ffmpeg, tmp) for _sid, w in banks.sources(e)[:64]]
                clip_cache[e] = [c for c in cl if len(c) > RATE // 50]
            return clip_cache[e]

        for key in sorted(inst, key=prio):
            evs = list(key[1]) if key[0] == 'walla' else [key[1]]
            rule = rules.get(evs[0].lower(), {})
            if loaded / 1e6 > a.budget_mb:
                print('w3ambience: over the %.0f MB budget; %s and the rest dropped' % (a.budget_mb, evs[0]))
                break
            idx = []
            if key[0] == 'walla':
                kind = 'walla'
                for e in evs:
                    cl = clips_of(e)
                    if not cl:
                        break
                    idx.append(add_sound(wav_name(e), bake(cl, 'loop', 60, None, rng)))
                if len(idx) != 4:
                    print('w3ambience: walla %s lacks media; dropped' % evs[0])
                    continue
            else:
                e = evs[0]
                cl = clips_of(e)
                if not cl:
                    print('w3ambience: %s has no media; dropped' % e)
                    continue
                kind = zone_kind(e, cl, rule)
                if kind == 'grains':
                    for k, c in enumerate(cl[:GRAIN_CLIPS]):
                        idx.append(add_sound('%s_%02d' % (wav_name(e)[:40], k), normalised(c)))
                else:
                    seconds = 300 if e.lower().startswith('mus_') else (60 if kind == 'loop' else 30)
                    idx.append(add_sound(wav_name(e), bake(cl, kind, seconds, event_spec(e)[4], rng)))
                    kind = 'loop'
            band = key[2] if len(key) > 2 else None
            hours = tuple(rule['hours']) if rule.get('hours') else name_hours(evs[0])
            if band:
                b = bands.get(band) or {}
                sites = b.get('sites') or []
                sh = sites[key[3]].get('hours') if key[3] < len(sites) else None
                hours = tuple(sh or b.get('hours') or (0.0, 0.0))
            gap = rule.get('gap_ms') or [int(g * 1000) for g in (event_spec(evs[0])[4] or DEFAULT_GAPS)]
            vol = rule.get('volume_db')
            if isinstance(vol, dict):
                vol = vol.get('chain') or [vol.get('base', 0.0), vol.get('rand_min', 0.0), vol.get('rand_max', 0.0)]
            vol = vol if isinstance(vol, (list, tuple)) and vol and not isinstance(vol[0], (list, dict)) else [0.0]
            gain = db_gain(float(vol[0]))
            vol_rand = min(1.0, max(0.0, 1.0 - db_gain(float(vol[1])))) if len(vol) > 1 and vol[1] else 0.15
            att = rule.get('attenuation') or {}
            reach = float(att.get('max_m') or reach_m[key]) * UPM
            flags = 0
            if rule.get('transition') == 'trigger_rate':
                flags |= 2                                 # the gap runs start to start
            if (rule.get('positioning') or {}).get('type') == 'listener_with_automation':
                flags |= 4                                 # one-shots round the listener
            si = sum(1 for x in sets if x.startswith('set '))
            sets.append('set %d %s %.3f %.0f %.2f %.2f %d %d %.2f %d %d %s' % (
                si, kind, gain, reach, hours[0], hours[1], int(gap[0]), int(gap[1]), vol_rand, flags, len(idx),
                ' '.join(map(str, idx))))
            curve = att.get('curve')
            if curve and len(curve) >= 2:
                mx = float(att.get('max_m') or curve[-1][0]) or 1.0
                pts = [(min(1.0, float(d) / mx), db_gain(float(db))) for d, db in curve][:8]
                sets.append('curve %d %d %s' % (si, len(pts), ' '.join('%.3f %.4f' % p for p in pts)))
            for kd, data in inst[key]:
                if kd == 'zone':
                    zmin, zmax, poly = data
                    lines_inst.append('zone %d %.0f %.0f %d %s' % (si, zmin, zmax, len(poly),
                                                                    ' '.join('%.0f %.0f' % q for q in poly)))
                else:
                    lines_inst.append('point %d %.0f %.0f %.0f' % ((si,) + tuple(data)))
            report[' '.join(evs) + ((' @' + band) if band else '')] = {
                'kind': kind, 'sounds': len(idx), 'places': len(inst[key]), 'reach_m': round(reach / UPM, 1),
                'hours': hours, 'gain': round(gain, 3), 'gap_ms': list(gap), 'flags': flags, 'rules': bool(rule)}
            print('  %-52s %-6s %2d sounds %3d places, %3.0f m, hours %s' % (
                evs[0][:52], kind, len(idx), len(inst[key]), reach / UPM, hours))
    text = ['amb 1', "# tools/levelport/w3ambience.py --zones: The Witcher 3's city ambience"]
    text += ['snd %d %s %d' % (i, pth, ms) for i, (pth, ms) in enumerate(sounds)]
    text += sets + lines_inst
    (stage / 'maps').mkdir(parents=True, exist_ok=True)
    (stage / 'maps' / ('%s.amb' % a.map)).write_text('\n'.join(text) + '\n', encoding='latin-1')
    pk3 = out / ('zz_%s_sound.pk3' % a.map)
    with zipfile.ZipFile(str(pk3) + '.tmp', 'w', zipfile.ZIP_DEFLATED) as z:
        for f in sorted(stage.rglob('*')):
            if f.is_file():
                z.write(f, f.relative_to(stage).as_posix())
    os.replace(str(pk3) + '.tmp', pk3)
    json.dump({'map': a.map, 'mode': 'zones', 'sets': len(report), 'sounds': len(sounds),
               'places': len(lines_inst), 'loaded_mb': round(loaded / 1e6, 1),
               'pk3_mb': round(pk3.stat().st_size / 1e6, 1), 'rules': a.rules, 'detail': report},
              open(out / 'w3ambience.json', 'w', encoding='utf-8'), indent=1)
    if not a.keep_stage:
        shutil.rmtree(stage)
    print('w3ambience --zones -> %s: %d sets, %d sounds, %d areas and points, %.1f MB once loaded, pk3 %.1f MB' % (
        pk3, len(report), len(sounds), len(lines_inst), loaded / 1e6, pk3.stat().st_size / 1e6))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--summary', required=True)
    ap.add_argument('--map-pk3', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--map', default='w3_city')
    ap.add_argument('--max-speakers', type=int, default=120)
    ap.add_argument('--budget-mb', type=float, default=150.0)
    ap.add_argument('--game', default=os.environ.get('W3_GAME', '<Steam library>/steamapps/common/The Witcher 3'))
    ap.add_argument('--keep-stage', action='store_true', help='keep <out>/stage after the pk3 is written')
    ap.add_argument('--zones', action='store_true', help="maps/<map>.amb for the client's ambience player, no speakers")
    ap.add_argument('--rules', help="wwise_rules.json: the containers' gaps, volumes, curves and hours (--zones)")
    a = ap.parse_args()
    if a.zones:
        zones_main(a, json.load(open(a.summary, encoding='utf-8'))['all'])
        return
    out = Path(a.out)
    stage = out / 'stage'
    if stage.exists():
        shutil.rmtree(stage)
    rows = json.load(open(a.summary, encoding='utf-8'))['all']
    chosen = place(rows, a.max_speakers)
    vgm = tool('vgmstream-cli', 'vgmstream.vgmstream_*/vgmstream-cli.exe')
    ffmpeg = tool('ffmpeg', 'Gyan.FFmpeg_*/ffmpeg-*/bin/ffmpeg.exe')
    banks = Banks(Cache(a.game))
    rng = np.random.default_rng(7)
    report, loaded, by_sources = {}, 0, {}
    with tempfile.TemporaryDirectory() as tmp:
        for e in sorted(chosen, key=lambda e: EVENTS[e][2]):
            mode, _cap, prio, seconds, gaps = EVENTS[e]
            srcs = banks.sources(e)
            key = tuple(sorted(s for s, _w in srcs))
            if key and key in by_sources:
                same = by_sources[key]
                report[e] = dict(report[same], loaded_mb=0.0,
                                 speakers=[[round(v, 1) for v in p] for p in chosen[e]])
                print('  %-52s shares %s, %2d speakers' % (e, same, len(chosen[e])))
                continue
            clips = [decode(w, vgm, ffmpeg, tmp) for _sid, w in srcs]
            clips = [c for c in clips if len(c) > RATE // 50]
            if not clips:
                print('w3ambience: %s has no media; dropped' % e)
                chosen.pop(e)
                continue
            if mode != 'loop' and max(len(c) for c in clips) >= LOOP_MIN_S * RATE:
                mode = 'loop'
            x = bake(clips, mode, seconds, gaps, rng)
            cost = len(x) / RATE * LOADED_RATE * 2
            if (loaded + cost) / 1e6 > a.budget_mb:
                print('w3ambience: %s (%.1f MB loaded) over the %.0f MB budget; dropped' % (e, cost / 1e6,
                                                                                         a.budget_mb))
                chosen.pop(e)
                continue
            loaded += cost
            by_sources[key] = e
            write_wav(stage / 'sound' / 'w3_city' / ('%s.wav' % wav_name(e)), x)
            report[e] = {'wav': wav_name(e), 'mode': mode, 'priority': prio, 'sources': [s for s, _w in srcs],
                         'clips': len(clips),
                         'seconds': round(len(x) / RATE, 1), 'loaded_mb': round(cost / 1e6, 2),
                         'speakers': [[round(v, 1) for v in p] for p in chosen[e]]}
            print('  %-52s %-6s %2d clips -> %5.1f s, %2d speakers' % (e, mode, len(clips), len(x) / RATE,
                                                                       len(chosen[e])))
    ents, n_bsp = entity_string(a.map_pk3, a.map)
    text = ents if ents.endswith('\n') else ents + '\n'
    text += ''.join(speaker(e, report[e]['wav'], p) for e in sorted(chosen) for p in chosen[e])
    (stage / 'maps').mkdir(parents=True, exist_ok=True)
    (stage / 'maps' / ('%s.ent' % a.map)).write_bytes(text.encode('latin-1'))
    pk3 = out / ('zz_%s_sound.pk3' % a.map)
    with zipfile.ZipFile(str(pk3) + '.tmp', 'w', zipfile.ZIP_DEFLATED) as z:
        for f in sorted(stage.rglob('*')):
            if f.is_file():
                z.write(f, f.relative_to(stage).as_posix())
    os.replace(str(pk3) + '.tmp', pk3)
    n_sp = sum(len(v) for v in chosen.values())
    summary = {'map': a.map, 'map_pk3': str(a.map_pk3), 'bsp_entities': n_bsp, 'speakers': n_sp,
               'events': len(report), 'loaded_mb': round(loaded / 1e6, 1), 'pk3_mb': round(pk3.stat().st_size / 1e6, 1),
               'rate': RATE, 'detail': report}
    json.dump(summary, open(out / 'w3ambience.json', 'w', encoding='utf-8'), indent=1)
    if not a.keep_stage:
        shutil.rmtree(stage)
    print('w3ambience -> %s: %d events, %d speakers (+%d map entities kept), %.1f MB once loaded, pk3 %.1f MB' % (
        pk3, len(report), n_sp, n_bsp, loaded / 1e6, pk3.stat().st_size / 1e6))


if __name__ == '__main__':
    main()
