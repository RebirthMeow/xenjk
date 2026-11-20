"""char_vram_check.py -- character texture memory checks (docs/renderer-character-vram-20261109.md).

    python -I <game_lock.py> python -I tools/char_vram_check.py --scene picker --arm after --tag p_after

Runs the installed GameData\\xenjk.x86.exe with its own UI, cgame and packs; only the renderer is the one under test
(<work>/dll/<arm>/rd-vulkan_x86.dll, loaded by a relative cl_renderer, so <work> must sit under the repository
root). Hidden window, muted, in_nograb, below normal priority, net off, private home under <work>/runs/<tag>.
Functional and memory figures only: nothing here is a timing result. Uses the picker checks' client harness
(.codex-research/wishlist-20261101/gpu-window/gw_common.py, not in this repository).

Environment: XENJK_MAIN (the checkout holding .codex-research; default this repository), GPU_WINDOW (the folder with
gw_common.py; default under XENJK_MAIN).

Scenes
  picker  the character picker browsed with posted keys: --rows Downs from the start model, every second row one Right
          (its second outfit), a rest after each; streaminfo, vkmem (where the renderer has it) and the GPU counters.
  eight   eight heroes as frozen NPCs on xenjk_showcase's deck (r_sunShadows 0: character shadow tiles, the path of
          character shadow tiles), seen from about 1000 units, then from close, then from far again; the same samples and
          screenshots at each view (far_a / far_b are the same still view 8 s apart: the pop check).
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(os.environ.get('XENJK_MAIN', Path(__file__).resolve().parents[1]))
WORK = ROOT / '.codex-research' / 'char-vram-20261109'
sys.path.insert(0, os.environ.get('GPU_WINDOW', str(ROOT / '.codex-research' / 'wishlist-20261101' / 'gpu-window')))
import gw_common as gw  # noqa: E402

DOWN, RIGHT, ESC = (0x28, 0x50, True), (0x27, 0x4D, True), (0x1B, 0x01, False)
EIGHT = ['ff7r_tifa__full4', 'ff7r_aerith__full4', 'cp_chloe_dressed', 'rita_v3', 'sb_eve', 'sb_tachy', 'mr_psylocke',
         'sb_eve_wk']
FAR, NEAR, CLOSE = 'setviewpos -380 0 40 0', 'setviewpos 100 0 40 0', 'setviewpos 170 0 40 0'   # (a teleport carries the player on about 116 units)


def gpu_counters(pid):
    """Dedicated and shared MB of the client (GPU Process Memory) and the adapter's dedicated use."""
    ps = ("$c=(Get-Counter '\\GPU Process Memory(*)\\Dedicated Usage','\\GPU Process Memory(*)\\Shared Usage',"
          "'\\GPU Adapter Memory(*)\\Dedicated Usage' -ErrorAction SilentlyContinue).CounterSamples;"
          "foreach($s in $c){ $s.Path + '=' + $s.CookedValue }")
    try:
        out = subprocess.run(['powershell', '-NoProfile', '-Command', ps], capture_output=True, text=True, timeout=40).stdout
    except Exception as e:  # noqa: BLE001
        return {'counter_error': str(e)}
    ded = sh = ad = 0.0
    for line in out.splitlines():
        path, _, val = line.rpartition('=')
        try:
            v = float(val)
        except ValueError:
            continue
        low = path.lower()
        if 'gpu process memory(pid_%d_' % pid in low:
            if 'dedicated' in low:
                ded += v
            else:
                sh += v
        elif 'gpu adapter memory' in low:
            ad = max(ad, v)
    return {'client_dedicated_MB': round(ded / 2**20, 1), 'client_shared_MB': round(sh / 2**20, 1),
            'adapter_dedicated_MB': round(ad / 2**20, 1)}


def parse(text):
    t = gw.clean(text)
    d = {}
    m = re.search(r'(\d+) streamed textures: (\d+) above their tails, (\d+) at them, (\d+) building, (\d+) failed', t)
    if m:
        d.update(streamed=int(m.group(1)), sharp=int(m.group(2)), at_tail=int(m.group(3)), building=int(m.group(4)),
                 failed=int(m.group(5)))
    m = re.search(r'tails ([\d.]+) MB; above them (\d+) of (\d+) MB; (\d+) MB uploaded, (\d+) builds, (\d+) dropped to the tail'
                  r'(?:, (\d+) stepped down)?', t)
    if m:
        d.update(tails_MB=float(m.group(1)), above_MB=int(m.group(2)), pool_MB=int(m.group(3)), uploaded_MB=int(m.group(4)),
                 builds=int(m.group(5)), dropped=int(m.group(6)), stepped=int(m.group(7) or 0))
    m = re.search(r'pressure: squeeze (\d+), (\d+) trimmed, (\d+) moved to empty a block; blocks (\d+) MB holding (\d+) MB; pool (.+)', t)
    if m:
        d.update(squeeze=int(m.group(1)), trimmed=int(m.group(2)), moved=int(m.group(3)), blocks_MB=int(m.group(4)),
                 blocks_used_MB=int(m.group(5)), pool_kind=m.group(6).strip())
    m = re.search(r'whole-loaded: (\d+) textures, (\d+) above their tails holding (\d+) MB; (\d+) reloads, (\d+) ms', t)
    if m:
        d.update(whole=int(m.group(1)), whole_sharp=int(m.group(2)), whole_MB=int(m.group(3)), reloads=int(m.group(4)),
                 reload_ms=int(m.group(5)))
    m = re.search(r'vkmem: ledger ([\d.]+) MB in VRAM, ([\d.]+) MB in system memory; (\d+) images loaded smaller', t)
    if m:
        d.update(ledger_MB=float(m.group(1)), ledger_sys_MB=float(m.group(2)), degraded=int(m.group(3)))
    m = re.search(r'vkmem: device-local heap (\d+) MB: budget ([\d.]+) MB, process use ([\d.]+) MB .*?free ([\d.]+) MB', t)
    if m:
        d.update(heap_MB=int(m.group(1)), budget_MB=float(m.group(2)), process_MB=float(m.group(3)), free_MB=float(m.group(4)))
    kinds = {}
    for m in re.finditer(r'vkmem: (.{34}) +([\d.]+) +([\d.]+) +([\d.]+) +(\d+)\s*$', t, re.M):
        kinds[m.group(1).strip()] = float(m.group(2))
    if kinds:
        d['kinds_MB'] = kinds
    return d


class Run:
    def __init__(self, args, rd):
        self.args, self.rd = args, rd
        self.samples = []
        self.t0 = time.time()

    def sample(self, c, steps, label):
        out = steps.run(c, ['streaminfo', 'vkmem', 'viewpos'], 60)
        s = {'label': label, 't': round(time.time() - self.t0, 1)}
        s.update(parse(out))
        m = re.search(r'\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) : ([-\d.]+)', gw.clean(out))
        if m:
            s['viewpos'] = ' '.join(m.groups())
        s.update(gpu_counters(c.p.pid))
        self.samples.append(s)
        print(json.dumps({k: v for k, v in s.items() if k != 'kinds_MB'}), flush=True)
        gw.write_json(self.rd / 'samples.json', self.samples)
        return s

    def shot(self, c, steps, name):
        r = c.screenshot(steps, name)
        # (the harness copies half a second after the file appears: a 1080p PNG is still being written then)
        src, last = self.rd / 'home' / 'base' / 'screenshots' / (name + '.png'), -1
        for _ in range(40):
            c.wait(0.25)
            size = src.stat().st_size if src.exists() else 0
            if size and size == last:
                break
            last = size
        if src.exists():
            shutil.copy2(src, self.rd / (name + '.png'))
        return r


def sets(args):
    s = gw.base_sets()
    s.update({'r_customwidth': '1920', 'r_customheight': '1080', 'r_ext_multisample': '4', 'developer': '1',
              'r_loadProfile': '0', 'com_maxfps': '60'})
    if args.scene == 'eight':
        s.update({'r_sunShadows': '0', 'cg_thirdPerson': '1', 'cg_draw2D': '0', 'cg_drawGun': '0', 'con_notifytime': '0',
                  'cg_thirdPersonVertOffset': '40'})
    for kv in args.set or []:
        k, _, v = kv.partition('=')
        s[k] = v
    return s


def picker(run, c, steps):
    a = run.args
    run.sample(c, steps, 'spawned')
    steps.run(c, ['ui_characters', 'echo CV_OPEN'], 30)
    c.wait(4.0)
    run.sample(c, steps, 'open')
    run.shot(c, steps, 'picker_open')
    for i in range(a.rows):
        c.key(*DOWN)
        c.wait(a.rest)
        if i % 2 == 1:
            c.key(*RIGHT)
            c.wait(a.rest)
        if i % a.every == a.every - 1 or i == a.rows - 1:
            run.sample(c, steps, 'row %d' % (i + 1))
    run.shot(c, steps, 'picker_last')
    c.wait(8.0)
    run.sample(c, steps, 'rested 8 s')
    c.key(*ESC)
    c.wait(15.0)
    run.sample(c, steps, 'closed 15 s')
    run.shot(c, steps, 'closed')
    out = steps.run(c, ['streaminfo all'], 60)
    (run.rd / 'streaminfo_all.txt').write_text(gw.clean(out), encoding='utf-8')
    out = steps.run(c, ['imagelist'], 60)
    (run.rd / 'imagelist.txt').write_text(gw.clean(out), encoding='utf-8')


def eight(run, c, steps):
    steps.run(c, ['d_npcfreeze 1', 'god', 'notarget', 'cg_thirdPersonAlpha 0'], 30)   # (the saber forces the third-person camera)
    run.sample(c, steps, 'spawned')
    for i in range(len(EIGHT)):
        y = -224 + 64 * i
        steps.run(c, ['setviewpos 600 %d 40 180' % y, 'wait 40', 'npc spawn cv_%d' % i, 'wait 60'], 60)
        c.wait(2.5)
    steps.run(c, [FAR], 30)
    c.wait(12.0)
    run.sample(c, steps, 'far 12 s')
    run.shot(c, steps, 'far_a')
    c.wait(8.0)
    run.sample(c, steps, 'far 20 s')
    run.shot(c, steps, 'far_b')
    steps.run(c, [NEAR], 30)
    c.wait(12.0)
    run.sample(c, steps, 'near 12 s')
    run.shot(c, steps, 'near_a')
    out = steps.run(c, ['streaminfo all'], 60)
    (run.rd / 'streaminfo_near.txt').write_text(gw.clean(out), encoding='utf-8')
    if run.args.debug_views:   # which map is wrong: the stage's colour alone, then the light's N.L
        for v in run.args.debug_views:
            steps.run(c, ['r_sunShadowDebug %d' % v, 'wait 30'], 30)
            run.shot(c, steps, 'near_dbg%d' % v)
        steps.run(c, ['r_sunShadowDebug 0', 'wait 30'], 30)
    c.wait(6.0)
    run.shot(c, steps, 'near_b')
    steps.run(c, [CLOSE], 30)
    c.wait(12.0)
    run.sample(c, steps, 'close 12 s')
    run.shot(c, steps, 'close_a')
    steps.run(c, [FAR], 30)
    c.wait(3.0)
    run.shot(c, steps, 'far_c')
    c.wait(17.0)
    run.sample(c, steps, 'far again 20 s')
    run.shot(c, steps, 'far_d')
    c.wait(10.0)
    run.sample(c, steps, 'far again 30 s')
    run.shot(c, steps, 'far_e')
    out = steps.run(c, ['streaminfo all'], 60)
    (run.rd / 'streaminfo_all.txt').write_text(gw.clean(out), encoding='utf-8')
    out = steps.run(c, ['imagelist'], 60)
    (run.rd / 'imagelist.txt').write_text(gw.clean(out), encoding='utf-8')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--scene', required=True, choices=['picker', 'eight'])
    ap.add_argument('--arm', required=True, help='<work>/dll/<arm>/rd-vulkan_x86.dll')
    ap.add_argument('--tag', required=True)
    ap.add_argument('--work', default=str(WORK))
    ap.add_argument('--set', action='append', help='cvar=value into the private autoexec.cfg')
    ap.add_argument('--rows', type=int, default=26)
    ap.add_argument('--every', type=int, default=2, help='picker: sample every N rows')
    ap.add_argument('--rest', type=float, default=4.0)
    ap.add_argument('--start', default='ff7r_tifa__full4/default')
    ap.add_argument('--debug-views', type=int, nargs='*', help='eight: r_sunShadowDebug views shot at the near view')
    args = ap.parse_args()
    work = Path(args.work)
    rd = work / 'runs' / args.tag
    if (rd / 'home' / 'base' / 'qconsole.log').exists():
        sys.exit('%s already ran: new --tag' % rd)
    dll = work / 'dll' / args.arm / 'rd-vulkan_x86.dll'
    if not dll.exists():
        sys.exit('no %s' % dll)
    rel = '../' * 3 + dll.parent.relative_to(ROOT).as_posix() + '/rd-vulkan'   # from GameData
    if (gw.GAME / (rel + '_x86.dll')).resolve() != dll.resolve():
        sys.exit('cl_renderer %s does not resolve to %s' % (rel, dll))
    free = {d: round(shutil.disk_usage(d + ':\\').free / 2**30, 1) for d in 'CD'}
    if min(free.values()) < 20:
        sys.exit('low disk: %s' % free)
    base = rd / 'home' / 'base'
    base.mkdir(parents=True, exist_ok=True)
    if args.scene == 'eight':
        npc = base / 'ext_data' / 'npcs'
        npc.mkdir(parents=True, exist_ok=True)
        txt = ''
        for i, m in enumerate(EIGHT):
            txt += ('cv_%d\n{\n\tplayerModel\t%s\n\tweapon\t\tWP_NONE\n\treactions\t3\n\taim\t\t\t1\n\tmove\t\t3\n'
                    '\taggression\t1\n\tevasion\t\t1\n\tintelligence\t5\n\trank\t\tcrewman\n\tplayerTeam\tTEAM_PLAYER\n'
                    '\tenemyTeam\tTEAM_ENEMY\n\tclass\t\tCLASS_PRISONER\n\twalkSpeed\t55\n\trunSpeed\t200\n\tyawspeed\t110\n}\n\n'
                    % (i, m))
        (npc / 'cv.npc').write_bytes(txt.replace('\n', '\r\n').encode('ascii'))
    start = args.start if args.scene == 'picker' else 'kyle/default'
    boot = ['set model ' + start, 'devmap ' + gw.MAP, 'wait 2500', 'togglemenu', 'wait 200', 'team free', 'wait 900',
            'echo GW_SPAWNED'] + gw.Steps.loop_lines(1)
    gw.write_cfgs(rd, sets(args), boot, 'gw_boot')
    gw.Steps(rd)
    cmd = [str(gw.EXE), '+set', 'fs_basepath', str(gw.GAME), '+set', 'fs_homepath', str(rd / 'home'),
           '+set', 'fs_game', 'base', '+set', 'cl_renderer', rel, '+set', 'com_unpackLibraries', '0',
           '+set', 'net_enabled', '0', '+set', 'net_ip', '127.0.0.1', '+set', 's_volume', '0', '+set', 's_musicvolume', '0',
           '+set', 's_mute', '1',
           '+set', 'logfile', '2', '+set', 'sv_cheats', '1', '+exec', 'gw_boot.cfg']
    info = {'scene': args.scene, 'arm': args.arm, 'dll_sha256': gw.sha256(dll), 'sets': sets(args), 'free_GB': free,
            'command': gw.cmdline_text(cmd), 'started': time.strftime('%Y-%m-%d %H:%M:%S'), 'label': gw.NOT_TIMING}
    gw.write_json(rd / 'run.json', info)
    ref = {}
    gw.guard_slot(ref)
    run = Run(args, rd)
    c = gw.Client(rd, cmd)
    ref['client'] = c
    err = None
    try:
        if not c.until(r'GW_SPAWNED', 300):
            raise RuntimeError('no GW_SPAWNED within 300 s')
        c.wait(3)
        info['modules'] = c.modules()
        steps = gw.Steps(rd)
        (picker if args.scene == 'picker' else eight)(run, c, steps)
    except Exception as e:  # noqa: BLE001
        err = '%s: %s' % (type(e).__name__, e)
    finally:
        info['diagnosis'] = c.diagnosis
        info['exit_code'] = c.stop()
        info['error'] = err
        info['wall_s'] = round(time.time() - run.t0, 1)
        info['crashlogs'] = gw.crashlogs_since(run.t0)
        log = gw.clean(c.text or '')
        info['warm_requests'] = re.findall(r'model warm-up \d+: (\S+) \(skin', log)
        info['log_flags'] = [l for l in log.splitlines()
                             if re.search(r'(?i)VRAM:|heap overflow|DEVICE_LOST|stream: (no image|reading|reloading)|squeeze', l)][:200]
        info['reload_lines'] = len(re.findall(r'reloaded in', log))
        gw.write_json(rd / 'run.json', info)
        gw.write_json(rd / 'samples.json', run.samples)
    print('error:', err, '| wall', info['wall_s'], 's | exit', info['exit_code'], '| crashlogs', info['crashlogs'])
    sys.exit(1 if err else 0)


if __name__ == '__main__':
    main()
