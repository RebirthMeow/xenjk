"""CP2077's Yaiba Kusanagi CT-3X sounds -> the bike's sound files (the bike's own sounds, taken from
CP2077). Local play only, as the rest of the port: nothing here is published.

CP2077 synthesises the player bike's engine at runtime from a plugin model (yaiba_asmx_kusanagi.bnk: one 'PLUG'/'ADM3'
blob, no recordings; neither vgmstream nor WolvenKit reads it). What is recorded, and used here:
  engine     v_mbike_yaiba_asmx_kusanagi_traffic_start: the layer container NPC Kusanagis play, steady beds and short
             loops at 43..66 Hz firing frequency, and the engine_on rev sweep (45..100 Hz). Rendered into ENGINE_STEPS
             loops of one length on a geometric firing-frequency ladder (FIRE_LO..FIRE_HI over idle..max rpm: a
             compressed mapping, the recordings are cruising revs), each from the steadiest source window nearest in
             pitch, resampled to the step's pitch and crossfaded into a seamless loop; an on-load and an off-load
             (low-passed, quieter) variant per step
  one-shots  ignition, start, stop, gear up and down, horn, standby: the bike's own events (its enter event starts
             the idle beds, which the loops cover; its exit event plays no recording)
  tyres      v_car_global_small_tyre_start_rr (the bike-size tyre container): a rolling loop and a squeal loop,
             picked by spectrum (rolling: broadband 1 s loops; squeal: tonal 1.5..1.9 kHz); its terrain twin
             (_terrain_start_rr) gives the rolling loop on dirt, grass and sand
  landing    v_car_global_susp_landing_bike variants

Before extract: no level-port fetch (cp2gltf) or WolvenKit may be reading the archives, and CP2077 must be closed.

Media: sfx_container.bnk (the hierarchy: Event -> Play action -> container -> Sounds, children by DirectParentID),
sfx_container.opusinfo (hash, pack, offset, header, length) and sfx_container_N.opuspak (Wwise Opus streams).

  python -I ct3x_audio.py extract   # WolvenKit 10 unbundles, vgmstream decodes into WORK/wav
  python -I ct3x_audio.py render    # WORK/out/sound/vehicles/ct3x/*.wav
  python -I ct3x_audio.py pack IN.pk3 OUT.pk3 [ct3x.bike]   # the bike pack plus the sounds, a .veh without the
                                                           # swoop's and the handling file (ct3x_params.py)

Environment: CP2077 (the game folder), CP_TOOLS_BUILD (folder with wolvenkit-10.0.0-beta.1 and dotnet10; needed by
extract), VGMSTREAM (vgmstream-cli, default from PATH), XENJK_MAIN (the checkout holding .codex-research; default this
repository), CT3X_AUDIO_WORK (the work folder).
"""
from __future__ import annotations

import collections
import glob
import os
import re
import struct
import subprocess
import sys
import wave
import zipfile
from pathlib import Path

import numpy as np
from scipy import signal

CP2077 = Path(os.environ.get('CP2077', r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'))
REPO = Path(__file__).resolve().parents[2]
MAIN = Path(os.environ.get('XENJK_MAIN', REPO))   # the checkout that holds .codex-research (a worktree's main checkout)
WORK = Path(os.environ.get('CT3X_AUDIO_WORK', MAIN / '.codex-research' / 'wishlist-20261101' / 'drafts' / 'bike'
                           / 'audio'))
VGMSTREAM = os.environ.get('VGMSTREAM', 'vgmstream-cli')   # on PATH unless given


def cp_tools():
    """the CP2077 tools build folder (WolvenKit 10 and its .NET), from CP_TOOLS_BUILD"""
    v = os.environ.get('CP_TOOLS_BUILD')
    if not v:
        raise SystemExit('set CP_TOOLS_BUILD to the CP2077 tools build folder (wolvenkit-10.0.0-beta.1, dotnet10)')
    return Path(v)


SOUNDBANKS = 'base\\sound\\soundbanks\\'

EVENTS = ('v_mbike_yaiba_asmx_kusanagi_traffic_start', 'v_mbike_yaiba_asmx_kusanagi_engine_on',
          'v_mbike_yaiba_asmx_kusanagi_engine_off', 'v_mbike_yaiba_asmx_kusanagi_ignition_on',
          'v_mbike_yaiba_asmx_kusanagi_gear_up', 'v_mbike_yaiba_asmx_kusanagi_gear_down',
          'v_mbike_yaiba_asmx_kusanagi_horn', 'v_mbike_yaiba_asmx_kusanagi_enter', 'v_mbike_yaiba_asmx_kusanagi_exit',
          'v_mbike_yaiba_asmx_kusanagi_standby', 'v_car_global_susp_landing_bike',
          'v_car_global_small_tyre_start_rr', 'v_car_global_small_tyre_terrain_start_rr')

# engine sources (event, src): the traffic layer's steady beds and loops, and engine_on's rev sweep
ENGINE_SOURCES = (('traffic_start', 418362349), ('traffic_start', 669850950), ('traffic_start', 539185327),
                  ('traffic_start', 316867713), ('traffic_start', 328947190), ('traffic_start', 629467269),
                  ('traffic_start', 583251120), ('engine_on', 685471251))
# one-shots: out name -> (event, src)
ONESHOTS = {'ignition': ('ignition_on', 325265015), 'start': ('engine_on', 171479131),
            'stop': ('engine_off', 689278311), 'gear_up': ('gear_up', 853181730),
            'gear_down': ('gear_down', 333298201), 'horn': ('horn', 52943166),
            'land0': ('susp_landing_bike', 121204471), 'land1': ('susp_landing_bike', 244999027),
            'land2': ('susp_landing_bike', 205584415), 'standby': ('standby', 894485526)}
# (enter starts the engine's idle beds, which the rpm loops cover; exit plays no recording in CP2077)
LOOPS = {'roll': ('small_tyre_start_rr', 797910813), 'skid': ('small_tyre_start_rr', 129608337),
         'roll_terrain': ('small_tyre_terrain_start_rr', 612523676)}

RATE = 22050
ENGINE_STEPS = 48
LOOP_S = 1.0
FIRE_LO, FIRE_HI = 40.0, 110.0      # Hz at idle and max rpm (ct3x.bike minRPM, maxRPM)
OFF_LOWPASS, OFF_GAIN = 1400.0, 0.55


# ---- CP2077 audio data

def fnv1(s: str) -> int:
    h = 2166136261
    for c in s.lower().encode():
        h = (h * 16777619) & 0xffffffff
        h ^= c
    return h


def wolvenkit(*args):
    tools = cp_tools()
    env = dict(os.environ, DOTNET_ROOT=str(tools / 'dotnet10'))
    subprocess.run([str(tools / 'wolvenkit-10.0.0-beta.1' / 'WolvenKit.CLI.exe'), *args], check=True, env=env,
                   stdout=subprocess.DEVNULL)


def unbundle(archive: str, regex: str):
    wolvenkit('unbundle', str(CP2077 / 'archive' / 'pc' / 'content' / archive), '-o', str(WORK / 'cp'), '-r', regex)


def hirc(path: Path) -> dict:
    b = path.read_bytes()
    p, objs = 0, {}
    while p + 8 <= len(b):
        tag, size = b[p:p + 4], struct.unpack_from('<I', b, p + 4)[0]
        if tag == b'HIRC':
            body = b[p + 8:p + 8 + size]
            q = 4
            for _ in range(struct.unpack_from('<I', body, 0)[0]):
                t = body[q]
                ln, oid = struct.unpack_from('<II', body, q + 1)
                objs[oid] = (t, body[q + 9:q + 5 + ln])
                q += 5 + ln
        p += 8 + size
    return objs


def parent_of(d: bytes, q: int) -> int:
    """NodeBaseParams at d[q:] (bank v150): FX, metadata, bus, then DirectParentID."""
    q += 1
    nfx = d[q]
    q += 1 + ((1 + 7 * nfx) if nfx else 0)
    q += 1
    nmeta = d[q]
    q += 1 + 6 * nmeta + 4
    return struct.unpack_from('<I', d, q)[0]


def event_sources(objs: dict, name: str) -> list[int]:
    children, sounds = collections.defaultdict(list), {}
    for oid, (t, d) in objs.items():
        try:
            if t == 2:
                plugin, stype, src = struct.unpack_from('<IBI', d, 0)
                q = 14 + ((4 + struct.unpack_from('<I', d, 14)[0]) if (plugin & 0x0f) == 2 else 0)
                sounds[oid] = src
                children[parent_of(d, q)].append(oid)
            elif t in (5, 6, 7, 9):
                children[parent_of(d, 0)].append(oid)
        except (struct.error, IndexError):
            pass

    def below(o):
        if o in sounds:
            yield sounds[o]
        for c in children.get(o, []):
            yield from below(c)

    t, d = objs[fnv1(name)]
    out = []
    for a in struct.unpack_from('<%dI' % d[0], d, 1):
        if a in objs and objs[a][0] == 3:
            atype, target = struct.unpack_from('<HI', objs[a][1], 0)
            if atype & 0xff00 == 0x0400:
                out += [s for s in below(target) if s not in out]
    return out


def opusinfo(path: Path) -> dict:
    b = path.read_bytes()
    n = struct.unpack_from('<I', b, 12)[0]
    p = 20
    arrays = []
    for fmt, w in (('I', 4), ('H', 2), ('I', 4), ('H', 2), ('I', 4)):
        arrays.append(struct.unpack_from('<%d%s' % (n, fmt), b, p))
        p += w * n
    return {arrays[0][i]: tuple(a[i] for a in arrays[1:]) for i in range(n)}


def extract():
    cp = WORK / 'cp' / 'base' / 'sound' / 'soundbanks'
    unbundle('audio_2_soundbanks.archive', r'sfx_container\.(bnk|opusinfo)$')
    objs, idx = hirc(cp / 'sfx_container.bnk'), opusinfo(cp / 'sfx_container.opusinfo')
    want = {ev: event_sources(objs, ev) for ev in EVENTS}
    packs = sorted({idx[s][0] for v in want.values() for s in v if s in idx})
    missing = [k for k in packs if not (cp / ('sfx_container_%d.opuspak' % k)).exists()]
    if missing:
        unbundle('audio_2_soundbanks.archive', r'sfx_container_(%s)\.opuspak$' % '|'.join(map(str, missing)))
    for ev, srcs in want.items():
        d = WORK / 'wav' / ev.replace('v_mbike_yaiba_asmx_kusanagi_', '').replace('v_car_global_', '')
        d.mkdir(parents=True, exist_ok=True)
        for s in srcs:
            if s not in idx:
                continue        # the engine plugin's source
            pk, off, _head, ln = idx[s]
            with open(cp / ('sfx_container_%d.opuspak' % pk), 'rb') as f:
                f.seek(off)
                (d / ('%d.wem' % s)).write_bytes(f.read(ln))
            subprocess.run([VGMSTREAM, '-o', str(d / ('%d.wav' % s)), str(d / ('%d.wem' % s))], check=True,
                           stdout=subprocess.DEVNULL)
        print(ev, len(srcs), 'sources')


# ---- rendering

def read_wav(path: Path) -> np.ndarray:
    w = wave.open(str(path))
    n, ch, rate = w.getnframes(), w.getnchannels(), w.getframerate()
    x = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float64).reshape(-1, ch).mean(1) / 32768.0
    return signal.resample_poly(x, RATE, rate) if rate != RATE else x


def write_wav(path: Path, x: np.ndarray):
    path.parent.mkdir(parents=True, exist_ok=True)
    w = wave.open(str(path), 'wb')
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes((np.clip(x, -1, 1) * 32767).astype(np.int16).tobytes())
    w.close()


def f0_track(x: np.ndarray, hop=0.05, win=0.12) -> np.ndarray:
    """The engine's fundamental (autocorrelation, 25..400 Hz) every hop seconds; 0 where unvoiced."""
    h, n = int(hop * RATE), int(win * RATE)
    lo, hi = int(RATE / 400), int(RATE / 25)
    out = []
    for i in range(0, len(x) - n, h):
        s = x[i:i + n] - x[i:i + n].mean()
        ac = np.fft.irfft(np.abs(np.fft.rfft(s, 2 * n)) ** 2)[:n]
        k = lo + int(np.argmax(ac[lo:hi]))
        out.append(RATE / k if ac[0] > 1e-9 and ac[k] > 0.3 * ac[0] else 0.0)
    return np.array(out)


def windows(name: str, x: np.ndarray, length: float):
    """(name, start sample, median f0, spread) for each length-second window that is voiced throughout."""
    f = f0_track(x)
    m = int(length / 0.05)
    for i in range(0, len(f) - m, 2):
        seg = f[i:i + m]
        if (seg > 0).all():
            yield name, int(i * 0.05 * RATE), float(np.median(seg)), float(np.std(np.log(seg)))


def repitch(x: np.ndarray, start: int, f0: float, f_target: float, n_out: int) -> np.ndarray:
    """n_out samples of x from start, resampled by f_target / f0 (the window's median fundamental). A warp along the
    measured pitch track was tried first: its octave errors on the drifting short loops broke the upper steps."""
    n_src = int(round(n_out * f_target / f0))
    return signal.resample(x[start:start + n_src], n_out)


def loopify(x: np.ndarray, n: int, fade: int) -> np.ndarray:
    """n samples that loop seamlessly: the fade samples after n crossfaded over the first fade."""
    y = x[:n].copy()
    r = np.linspace(0.0, 1.0, fade)
    y[:fade] = x[:fade] * r + x[n:n + fade] * (1.0 - r)
    return y


def norm(x: np.ndarray, rms: float) -> np.ndarray:
    return x * (rms / max(1e-9, float(np.sqrt((x * x).mean()))))


def render():
    src = WORK / 'wav'
    out = WORK / 'out' / 'sound' / 'vehicles' / 'ct3x'
    n, fade = int(LOOP_S * RATE), int(0.12 * RATE)
    cands, audio = [], {}
    for ev, s in ENGINE_SOURCES:
        x = read_wav(src / ev / ('%d.wav' % s))
        x = signal.sosfiltfilt(signal.butter(2, 20, 'hp', fs=RATE, output='sos'), x)
        audio['%s/%d' % (ev, s)] = x
        cands += list(windows('%s/%d' % (ev, s), x, 1.6 * LOOP_S))
    lp = signal.butter(2, OFF_LOWPASS, 'lp', fs=RATE, output='sos')
    for i in range(ENGINE_STEPS):
        ft = FIRE_LO * (FIRE_HI / FIRE_LO) ** (i / (ENGINE_STEPS - 1))
        name, start, f0, _sp = min(cands, key=lambda c: abs(np.log(ft / c[2])) + 2.0 * c[3])
        y = repitch(audio[name], start, f0, ft, n + fade)
        y = norm(loopify(y, n, fade), 0.16 * (0.8 + 0.4 * i / (ENGINE_STEPS - 1)))
        write_wav(out / ('eng_on_%02d.wav' % i), y)
        write_wav(out / ('eng_off_%02d.wav' % i), OFF_GAIN * signal.sosfiltfilt(lp, y))
        print('step %2d %6.1f Hz from %s at %.1f s (%.1f Hz)' % (i, ft, name, start / RATE, f0))
    for o, (ev, s) in ONESHOTS.items():
        x = read_wav(src / ev / ('%d.wav' % s))
        nz = np.nonzero(np.abs(x) > 0.01)[0]
        x = x[nz[0]:nz[-1] + 1] if len(nz) else x
        write_wav(out / (o + '.wav'), norm(x, 0.2) if o != 'horn' else norm(x, 0.25))
    for o, (ev, s) in LOOPS.items():
        x = read_wav(src / ev / ('%d.wav' % s))
        m = len(x) - fade
        write_wav(out / (o + '.wav'), norm(loopify(x, m, fade), 0.12 if o == 'roll' else 0.18))
    print('wrote', len(list(out.glob('*.wav'))), 'files to', out)


# ---- the pack

VEH = 'ext_data/vehicles/ct3x.veh'
BIKE = 'ext_data/vehicles/ct3x.bike'   # its handling (bg_bikemove.c BG_BikeLoadParams)
VEH_DROP = ('soundLoop', 'soundOn', 'soundOff', 'soundEngineStart', 'bikeDynamics')   # cgame plays them now


def pack(src: str, dst: str, bike: str | None = None):
    """dst = src with the rendered sounds and a .veh of type VH_BIKE (BikeNPC.c) without the swoop's sounds: cgame
    plays the bike's (cg_bike.c CG_BikeSound: the engine by rpm, ignition and start on boarding, stop on exit).
    bikeDynamics was v2's key; VH_BIKE is the type now. bike: a handling file (ct3x_params.py) to carry as BIKE."""
    if os.path.exists(dst) and os.path.samefile(src, dst):
        raise SystemExit('pack: IN and OUT are the same file; write a new pack')
    out = WORK / 'out'
    with zipfile.ZipFile(src) as zi, zipfile.ZipFile(dst, 'w') as zo:
        for info in zi.infolist():
            data = zi.read(info)
            if info.filename == VEH:
                text = data.decode('latin1')
                for k in VEH_DROP:
                    text = re.sub(r'(?m)^%s\s.*\r?\n' % k, '', text)
                text = re.sub(r'(?m)^type\s.*$', 'type\t\t\tVH_BIKE', text)
                data = text.encode('latin1')
            if bike and info.filename == BIKE:
                continue
            zo.writestr(info, data, compress_type=info.compress_type)
        if bike:
            zo.write(bike, BIKE, compress_type=zipfile.ZIP_DEFLATED)
        for p in sorted(out.rglob('*.wav')):
            zo.write(p, p.relative_to(out).as_posix(), compress_type=zipfile.ZIP_STORED)
    print('wrote', dst)


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'extract':
        extract()
    elif cmd == 'render':
        render()
    elif cmd == 'pack':
        pack(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else None)
    else:
        print(__doc__)
