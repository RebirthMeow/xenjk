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
"""Screenshots of a levelport map in the installed client (GameData/xenjk.x86.exe), hidden, muted, private homepath.

    python tools/levelport/look.py <pk3> <mapname> [--views 8] [--out DIR] [--size 1600x900] [--set "cvar value"]

The pk3 is staged into the run's own homepath, so nothing is installed. Views are a spectator's (no body or saber;
--player for a player's) from spawn points spread across the map (farthest-point order), each looking toward the map's
centre, or --viewpos "x y z yaw [pitch]" views of your own. Writes a contact sheet.
"""
import argparse
import math
import os
import shutil
import sys
import time
import zipfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
import paths  # noqa: E402
import q3bsp  # noqa: E402
import renderer_bench as rb  # noqa: E402


def stage(src, dst):
    """A hard link to the build (its bytes stored once: each run used to copy ~0.9 GB); a copy across drives."""
    dst = Path(dst)
    try:
        if dst.exists():
            dst.unlink()
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def views_from_bsp(data, n):
    import tempfile
    with tempfile.NamedTemporaryFile(suffix='.bsp', delete=False) as f:
        f.write(data)
    try:
        bsp = q3bsp.Bsp(f.name)
    finally:
        Path(f.name).unlink()
    pts = [np.array([float(c) for c in e['origin'].split()]) for e in bsp.entities
           if e.get('classname', '').startswith(('info_player', 'team_CTF_'))
           and 'origin' in e and 'flag' not in e.get('classname', '')]
    if not pts:
        return []
    pts = np.array(pts)
    centre = (bsp.models[0]['mins'] + bsp.models[0]['maxs']) / 2
    order = [0]
    while len(order) < min(n, len(pts)):
        d = np.min(np.linalg.norm(pts[:, None] - pts[order][None], axis=2), axis=1)
        order.append(int(np.argmax(d)))
    out = []
    for i in order:
        p = pts[i]
        yaw = math.degrees(math.atan2(centre[1] - p[1], centre[0] - p[0]))
        out.append((p, yaw))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('pk3')
    ap.add_argument('map')
    ap.add_argument('--out', default=None)
    ap.add_argument('--views', type=int, default=8)
    ap.add_argument('--size', default='1600x900')
    ap.add_argument('--set', action='append', default=[])
    ap.add_argument('--timeout', type=int, default=240)
    ap.add_argument('--renderer', default='rd-vulkan')
    ap.add_argument('--cvar', action='append', default=[], help='name=value set in autoexec, before the map loads')
    ap.add_argument('--extra-pk3', action='append', default=[], help='another pk3 staged beside the map (a render scene)')
    ap.add_argument('--viewpos', action='append', default=[],
                    help='"x y z yaw [pitch]": a view of your own in place of the spawns (pitch in degrees, up positive)')
    ap.add_argument('--player', action='store_true', help='look as a player (body and saber in view) instead of a spectator')
    ap.add_argument('--bin64', action='store_true', help="the 64-bit client (GameData/bin64, the Play_*.bat one): a "
                    "scene whose file is larger than the 32-bit client can allocate")
    ap.add_argument('--png', action='store_true', help='lossless screenshots (a debug view read per pixel)')
    ap.add_argument('--exe', help='a client to run in place of the installed one, from its own folder (SDL2.dll and '
                    'the renderer beside it); the installed GameData stays the base')
    ap.add_argument('--game-dll', action='append', default=[],
                    help="a module (cgame/ui DLL) staged in the run's home base, where the client looks first")
    ap.add_argument('--hold', type=int, default=0, help='extra milliseconds at each view before its screenshot')
    a = ap.parse_args(argv)
    out = Path(a.out or paths.LEVELPORT / 'look' / ('%s-%s' % (a.map, time.strftime('%Y%m%d-%H%M%S')))).resolve()
    base = out / 'home' / 'base'
    base.mkdir(parents=True, exist_ok=True)
    for p in [a.pk3] + a.extra_pk3:
        stage(p, base / Path(p).name)
    for p in a.game_dll:
        shutil.copy2(p, base / Path(p).name)
    with zipfile.ZipFile(a.pk3) as z:
        views = views_from_bsp(z.read('maps/%s.bsp' % a.map), a.views)
    pitches = {}
    if a.viewpos:
        views = [(np.array([float(v) for v in vp.split()[:3]]) - (0, 0, 16), float(vp.split()[3])) for vp in a.viewpos]
        pitches = {k: float(vp.split()[4]) for k, vp in enumerate(a.viewpos) if len(vp.split()) > 4}
    w, h = a.size.split('x')
    sets = {'r_mode': '-1', 'r_customwidth': w, 'r_customheight': h, 'r_fullscreen': '0', 'r_noborder': '1',
            's_volume': '0', 's_musicvolume': '0', 'cl_motd': '0', 'net_enabled': '0', 'cl_autoRecordDemo': '0',
            'com_maxfps': '120', 'r_screenshotJpegQuality': '90', 'g_gametype': '0', 'cg_fov': '95',
            'con_notifytime': '0', 'cg_draw2D': '0', 'cg_drawGun': '0', 'cg_thirdPerson': '0',
            'g_spawnInvulnerability': '0',   # the spawn shield's green bubble sat in some shots, not others
            'cl_renderer': a.renderer}
    sets.update(c.split('=', 1) for c in a.cvar)
    (base / 'autoexec.cfg').write_text(''.join('seta %s "%s"\n' % kv for kv in sets.items()), encoding='utf-8')
    # a spectator draws no body and no saber (a saber forces third person, and its blade lights the scene)
    join = ['team free', 'wait 300', 'noclip'] if a.player else ['team spectator', 'wait 300']
    lines = ['devmap %s' % a.map, 'wait 2500', 'togglemenu', 'wait 100', *join, *a.set, 'wait 100']
    for k, (p, yaw) in enumerate(views):
        lines += ['setviewpos %d %d %d %d' % (p[0], p[1], p[2] + 16, yaw), 'wait 100']
        if pitches.get(k):
            # setviewpos levels the view; tilt it by holding look-up/down at 30 deg/s for the frames it takes at the
            # capped frame rate (close, not exact: the 'viewpos' line in qconsole.log has the angle it reached)
            frames = round(abs(pitches[k]) / 30.0 * float(sets['com_maxfps']))
            key = 'lookup' if pitches[k] > 0 else 'lookdown'
            lines += ['cl_pitchspeed 30', '+' + key, 'wait %d' % frames, '-' + key, 'cl_pitchspeed 140']
        if a.hold:
            lines += ['wait %d' % max(1, round(a.hold / 1000.0 * float(sets['com_maxfps'])))]
        lines += ['wait 200', 'viewpos', ('screenshot_png v%02d' if a.png else 'screenshot v%02d') % k, 'wait 60']
    lines += ['echo LOOK done', 'wait 30', 'quit']
    (base / 'look.cfg').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    if a.exe:
        exe = Path(a.exe).resolve()
        cwd = exe.parent
    elif a.bin64:
        exe, cwd = rb.ASSETS / 'bin64' / 'xenjk.x86_64.exe', rb.ASSETS / 'bin64'
    else:
        exe, cwd = rb.ASSETS / 'xenjk.x86.exe', rb.ASSETS
    args = [str(exe), '+set', 'fs_basepath', str(rb.ASSETS), '+set', 'fs_homepath',
            str(out / 'home'), '+set', 'fs_game', 'base', '+set', 'logfile', '1', '+set', 'sv_cheats', '1',
            '+set', 'cl_renderer', a.renderer,
            '+set', 'in_mouse', '0', '+set', 'in_nograb', '1']   # (the look client would otherwise grab the mouse)
    for c in a.cvar:               # latched cvars (the renderer) only take effect from the command line
        args += ['+set', *c.split('=', 1)]
    args += ['+exec', 'look.cfg']
    fg = rb.foreground_window()
    proc = rb.client_popen(args, str(cwd))
    t0, status = time.time(), 'timeout'
    while time.time() - t0 < a.timeout:
        rb.hide_client_windows(proc.pid, fg)
        if proc.poll() is not None:
            status = 'exited %d' % proc.returncode
            break
        time.sleep(0.1)
    rb.stop_client(proc)
    shots = sorted(p for p in (base / 'screenshots').glob('*') if p.suffix in ('.jpg', '.png', '.tga'))
    print('%s after %.0f s, %d screenshots in %s' % (status, time.time() - t0, len(shots), base / 'screenshots'))
    if shots:
        from PIL import Image
        tw, th = 640, int(640 * int(h) / int(w))
        thumbs = [Image.open(p).convert('RGB').resize((tw, th)) for p in shots]
        sheet = Image.new('RGB', (tw * 2, th * ((len(thumbs) + 1) // 2)))
        for i, t in enumerate(thumbs):
            sheet.paste(t, ((i % 2) * tw, (i // 2) * th))
        sheet.save(out / 'sheet.jpg', quality=88)
        print('sheet: %s' % (out / 'sheet.jpg'))
    log = base / 'qconsole.log'
    if log.exists():
        seen = set()
        for line in log.read_text(encoding='utf-8', errors='replace').splitlines():
            if any(k in line for k in ('WARNING', 'ERROR', 'could not', "couldn't")) and line not in seen:
                seen.add(line)
                if len(seen) <= 30:
                    print('  log:', line[:160])
        print('  %d distinct warning lines' % len(seen))


if __name__ == '__main__':
    main()
