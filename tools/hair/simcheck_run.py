"""simcheck_run.py <tag> --dll <path to rd-vulkan_x86.dll> [--model aerith] [--map xenjk_showcase] [--out DIR]

Scripted check client for the per-frame hair / cloth decision and the step plan (rd-common/tr_sim.h, sim_frame.h;
record: docs/cloth/records/2026-11-09-hair-cloth-views-and-dropped-steps.md). One hidden, muted launch of the installed
GameData/xenjk.x86.exe with a dev renderer DLL, private home, no network, no mouse grab. The player's own model (a
character with a .chains and a .cloth sidecar) runs a circle in third person; the camera is slid sideways
(cg_thirdPersonHorzOffset) to take the model out of the main view while its shadow views still draw it.

Every arm sets r_simPerFrame / r_simSteps / r_clothGpu live and runs four phases; each phase is one r_simCheck window
(SIMCHECK lines in qconsole.log, between SIMPHASE begin/end marks):
  inview   the model on screen (control)
  culled   the model outside the main view, its shadow views still drawing it
  whip     out of view for ~300 ms, back for ~500 ms, six times
  hitch    six single ~200 ms frames (com_maxfps 5), ~0.6 s apart
  slow     a stretch at 5 fps (frames too long for the caps: a lasting shortage)
Prints one row per arm, phase and system; the run folder also gets simcheck.json. --dump records a hair dump through
the hitch phase instead (stretched steps included), for the offline replay.

Needs tools/renderer_scenes.py's launcher (run from the main checkout's tools directory through this file's own path).
"""
import argparse
import json
import os
import re
import sys
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('tag')
ap.add_argument('--dll', required=True, help='the dev rd-vulkan_x86.dll')
ap.add_argument('--model', default='aerith')
ap.add_argument('--map', default='xenjk_showcase')
ap.add_argument('--spot', default='0 0 40 0', help='setviewpos x y z yaw of the circle')
ap.add_argument('--out', default=None)
ap.add_argument('--root', default=str(Path(__file__).resolve().parents[2]),
                help='the checkout whose tools/ and game_directory/ are used (default this repository)')
ap.add_argument('--horz', type=float, default=700.0, help='cg_thirdPersonHorzOffset that takes the model out of view')
ap.add_argument('--arms', default='00g,11g,10g,01g,00c,11c', help='perFrame, steps, g(pu)/c(pu) cloth per arm')
ap.add_argument('--dump', action='store_true', help='no arms: record r_hairDump (dumps/simcheck_hair.hcd in the run home) '
                'through the hitch phase with the defaults, to replay offline (hcd_to_hcr.py, hc_replay)')
ap.add_argument('--set', action='append', default=[])
a = ap.parse_args()

ROOT = Path(a.root)
sys.path.insert(0, str(ROOT / 'tools'))
import renderer_scenes as rs  # noqa: E402

out = Path(a.out) if a.out else ROOT / '.codex-research' / 'hair-cloth-views-20261109' / 'runs'
rd = out / a.tag
if rd.exists():
    raise SystemExit('%s already ran: new tag' % rd)
(rd / 'home' / 'base').mkdir(parents=True)

# the installed exe loads <GameData>/<cl_renderer>_x86.dll: a path relative to GameData reaches the dev build
dll = Path(a.dll).resolve()
rel = os.path.relpath(str(dll)[:-len('_x86.dll')], str(rs.rb.ASSETS)).replace('\\', '/')

W = 250   # console 'wait' ticks a second at com_maxfps 125 (two command passes a frame)


def window(arm, phase, body):
    """one r_simCheck window: setting the cvar to another value prints what it holds and starts over"""
    return ['echo SIMPHASE begin %s %s' % (arm, phase), 'set r_simCheck 600', 'wait 6', 'viewpos'] + body + [
        'echo SIMPHASE end %s %s' % (arm, phase), 'set r_simCheck 601', 'wait 6']


L = ['set model %s/default' % a.model, 'devmap ' + a.map, 'wait 2500', 'togglemenu', 'wait 200', 'team free', 'wait 900',
     'set model %s/default' % a.model, 'wait 600', 'notarget', 'god',
     'exec xenjk_showcase' if a.map == 'xenjk_showcase' else 'wait 1', 'set cg_thirdPerson 1', 'wait 300',
     'sv_saberswitch', 'wait 300', 'set com_unfocused 0', 'clear',
     'setviewpos ' + a.spot, 'set cg_thirdPersonAngle 100', 'set cg_thirdPersonRange 120', 'set cg_thirdPersonVertOffset -10',
     'set cl_yawspeed 70', 'wait 300', 'echo SIMRUN start', 'viewpos', '+forward', '+left', 'wait %d' % W, 'viewpos',
     'screenshot simcheck_inview', 'wait 30', 'set cg_thirdPersonHorzOffset %g' % a.horz, 'wait 60',
     'screenshot simcheck_culled', 'wait 30', 'set cg_thirdPersonHorzOffset 0', 'wait 60']
if a.dump:
    # off for longer than the reset gap: the dump then starts with a reset record, which the replay needs
    L += ['set r_hair 0', 'wait %d' % W, 'set r_hairDump dumps/simcheck_hair.hcd', 'set r_hair 1', 'wait %d' % W]
    for _ in range(4):
        L += ['set com_maxfps 5', 'wait 2', 'set com_maxfps 125', 'wait %d' % int(0.6 * W)]
    L += ['set com_maxfps 5', 'wait 12', 'set com_maxfps 125', 'wait %d' % int(0.4 * W), 'set r_hairDump ""', 'wait 30']
for arm in ([] if a.dump else a.arms.split(',')):
    pf, st, solver = arm[0], arm[1], arm[2]
    L += ['set r_simPerFrame ' + pf, 'set r_simSteps ' + st, 'set r_clothGpu %d' % (solver == 'g'),
          'set cg_thirdPersonHorzOffset 0', 'wait %d' % W]
    L += window(arm, 'inview', ['wait %d' % (3 * W)])
    L += ['set cg_thirdPersonHorzOffset %g' % a.horz, 'wait 40']
    L += window(arm, 'culled', ['wait %d' % (3 * W)])
    L += ['set cg_thirdPersonHorzOffset 0', 'wait %d' % W]
    whip = []
    for _ in range(6):
        whip += ['set cg_thirdPersonHorzOffset %g' % a.horz, 'wait %d' % int(0.3 * W), 'set cg_thirdPersonHorzOffset 0',
                 'wait %d' % int(0.5 * W)]
    L += window(arm, 'whip', whip)
    hitch = []
    for _ in range(6):
        hitch += ['set com_maxfps 5', 'wait 2', 'set com_maxfps 125', 'wait %d' % int(0.6 * W)]
    L += window(arm, 'hitch', hitch)
    # at 5 fps a frame holds many command passes (the server's own frames), so 'wait' is far shorter than at 125 fps
    L += window(arm, 'slow', ['set com_maxfps 5', 'wait 90', 'set com_maxfps 125', 'wait %d' % int(0.4 * W)])
L += ['-forward', '-left', 'echo BENCHSCENE done', 'wait 30', 'quit']

sets = {'r_mode': '-1', 'r_customwidth': '1280', 'r_customheight': '720', 'r_fullscreen': '0', 'r_swapInterval': '0',
        'com_maxfps': '125', 'com_maxfpsUnfocused': '0', 's_volume': '0', 's_musicvolume': '0', 's_mute': '1', 'cl_motd': '0',
        'net_enabled': '0', 'cl_autoRecordDemo': '0', 'cg_draw2D': '0', 'cg_drawGun': '0', 'cg_fov': '80', 'g_gametype': '0',
        'bot_enable': '0', 'r_noborder': '1', 'vid_xpos': '0', 'vid_ypos': '0', 'in_nograb': '1', 'in_mouse': '0',
        'cl_renderer': rel, 'con_notifytime': '0', 'cg_chatBox': '0', 'sv_pure': '0', 'r_fbo': '1',
        'r_hair': '1', 'r_cloth': '1', 'r_hairSprings': '1', 'r_hairChains': '1'}
for kv in a.set:
    k, v = kv.split('=', 1)
    sets[k] = v
cand = {'name': 'xenjk-installed', 'family': 'vulkan-pbr', 'renderer': 'rd-vulkan', 'exe': 'xenjk.x86.exe', 'installed': True}
wall, first, status = rs.launch_scene(dict(cand, hide_window=True), rd, '\n'.join(L) + '\n', sets, 900, hide=True)

log = rs.written(rd, 'qconsole.log') or (rd / 'home' / 'base' / 'qconsole.log')
text = log.read_text(encoding='utf-8', errors='replace') if log.exists() else ''
rows, cur, closing = [], None, False
pat = re.compile(r'SIMCHECK (\w+) perFrame (\d) steps (\d) \| frames (\d+) advances (\d+) steps (\d+) capped (\d+) stretched (\d+) '
                 r'dropped (-?[\d.]+) ms \| per step: pose (-?[\d.]+) ms \(x([\d.]+) of what the solver was told\), input jump ([\d.]+) \| '
                 r'inside: samples (\d+) mean ([\d.]+) max (\d+) deepest ([\d.]+) \| draws (\d+) stale (\d+) \(oldest (\d+) frames\) '
                 r'refused (\d+) fallback (\d+), frames with any (\d+)')
keys = ['sys', 'perFrame', 'stepMode', 'frames', 'advances', 'steps', 'capped', 'stretched', 'droppedMs', 'poseMs', 'velRatio', 'jump',
        'insideSamples', 'insideMean', 'insideMax', 'insideDeepest', 'draws', 'stale', 'oldest', 'refused', 'fallback', 'staleFrames']
views = re.findall(r'\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) : ([-\d.]+)', text)
for line in text.splitlines():
    m = re.search(r'SIMPHASE (begin|end) (\S+) (\S+)', line)
    if m:
        cur, closing = (m.group(2), m.group(3)), m.group(1) == 'end'
        continue
    m = pat.search(line)
    if m and cur and closing:   # the window an 'end' mark closed
        d = dict(zip(keys, m.groups()))
        d.update(arm=cur[0], phase=cur[1])
        rows.append(d)
moved = len(set(views)) >= 2
info = {'status': status, 'wall': round(wall, 1), 'model': a.model, 'map': a.map, 'dll': str(dll), 'renderer_arg': rel, 'moved': moved,
        'positions': len(set(views)), 'rows': rows, 'warnings': [x for x in text.splitlines() if 'WARNING' in x.upper()][:20]}
(rd / 'simcheck.json').write_text(json.dumps(info, indent=1))
print(a.tag, status, round(wall), 'moved' if moved else 'PLAYER DID NOT MOVE: the run measures nothing', flush=True)
print('%-4s %-7s %-6s %6s %5s %5s %6s %7s %8s %7s %8s %7s %6s %6s %6s %7s %8s %6s' % (
    'arm', 'phase', 'sys', 'frames', 'adv', 'steps', 'capped', 'dropMs', 'poseMs', 'velX', 'jump', 'inMean', 'inMax', 'draws', 'stale',
    'oldest', 'refused', 'fallbk'))
for d in rows:
    print('%-4s %-7s %-6s %6s %5s %5s %6s %7s %8s %7s %8s %7s %6s %6s %6s %7s %8s %6s' % (
        d['arm'], d['phase'], d['sys'], d['frames'], d['advances'], d['steps'], d['capped'], d['droppedMs'], d['poseMs'], d['velRatio'],
        d['jump'], d['insideMean'], d['insideMax'], d['draws'], d['stale'], d['oldest'], d['refused'], d['fallback']))
