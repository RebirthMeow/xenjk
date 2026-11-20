"""Screenshots of xenjk_showcase in the installed client (GameData/xenjk.x86.exe), hidden, muted, private homepath.

    python tools/showcase_map/look.py <pk3> [--out DIR] [--model jan] [--size 1920x1080] [--set cvar=value ...]

The pk3 is staged into the run's own homepath (searched before the installed base), so nothing is installed.
Views are third-person shots of the player model: on the deck stage (facing +x into the sun) and in the studio
(facing -x, the cyclorama behind). Writes DIR/home/base/screenshots/*.jpg and a contact sheet DIR/sheet.jpg.
"""
import argparse
import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import renderer_bench as rb   # noqa: E402

ROOT = Path(__file__).resolve().parent.parent.parent

# (name, setviewpos x y z yaw, cg_thirdPersonAngle, range, vertical offset, pitch offset)
VIEWS = [
    ('deck_front', (0, 0, 40, 0), 160, 110, 6, 0),
    ('deck_front_r', (0, 0, 40, 0), 210, 110, 6, 0),
    ('deck_back', (0, 0, 40, 0), 0, 130, 10, 0),
    ('deck_wide', (0, 0, 40, 0), 170, 340, 60, 8),
    ('studio_front', (0, 0, -1560, 180), 180, 120, 6, 0),
    ('studio_34', (0, 0, -1560, 180), 145, 120, 6, 0),
    ('studio_wide', (0, 0, -1560, 180), 180, 300, 40, 6),
]


def cfg(model, extra):
    lines = ['devmap xenjk_showcase', 'wait 2500', 'togglemenu', 'wait 100', 'team free', 'wait 300', 'model %s' % model, 'wait 300',
             'exec xenjk_showcase', *('%s' % e for e in extra), 'wait 100']
    for name, pos, ang, rng, vo, po in VIEWS:
        lines += ['setviewpos %d %d %d %d' % pos, 'cg_thirdPersonAngle %d' % ang, 'cg_thirdPersonRange %d' % rng,
                  'cg_thirdPersonVertOffset %d' % vo, 'cg_thirdPersonPitchOffset %d' % po, 'wait 400',
                  'screenshot %s' % name, 'wait 60']
    lines += ['echo SHOWCASE done', 'wait 30', 'quit']
    return '\n'.join(lines) + '\n'


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('pk3')
    ap.add_argument('--out', default=str(ROOT / '.codex-research' / 'showcase_map' / 'look' / time.strftime('%Y%m%d-%H%M%S')))
    ap.add_argument('--model', default='jan')
    ap.add_argument('--size', default='1920x1080')
    ap.add_argument('--set', action='append', default=[], help='extra console line, e.g. "r_sunShadows 0"')
    ap.add_argument('--timeout', type=int, default=240)
    a = ap.parse_args(argv)
    out = Path(a.out).resolve()
    base = out / 'home' / 'base'
    base.mkdir(parents=True, exist_ok=True)
    shutil.copy2(a.pk3, base / Path(a.pk3).name)
    w, h = a.size.split('x')
    sets = {'r_mode': '-1', 'r_customwidth': w, 'r_customheight': h, 'r_fullscreen': '0', 'r_noborder': '1',
            's_volume': '0', 's_musicvolume': '0', 's_mute': '1', 'cl_motd': '0', 'net_enabled': '0', 'cl_autoRecordDemo': '0',
            'com_maxfps': '120', 'r_screenshotJpegQuality': '92', 'g_gametype': '0', 'cg_fov': '80', 'con_notifytime': '0'}
    (base / 'autoexec.cfg').write_text(''.join('seta %s "%s"\n' % kv for kv in sets.items()), encoding='utf-8')
    (base / 'look.cfg').write_text(cfg(a.model, [s.replace('=', ' ', 1) for s in a.set]), encoding='utf-8')
    args = [str(rb.ASSETS / 'xenjk.x86.exe'), '+set', 'fs_basepath', str(rb.ASSETS), '+set', 'fs_homepath',
            str(out / 'home'), '+set', 'fs_game', 'base', '+set', 'logfile', '1', '+set', 'sv_cheats', '1',
            '+exec', 'look.cfg']
    fg = rb.foreground_window()
    proc = rb.client_popen(args, str(rb.ASSETS))
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
        thumbs = [Image.open(p).convert('RGB').resize((640, 360)) for p in shots]
        sheet = Image.new('RGB', (1280, 360 * ((len(thumbs) + 1) // 2)))
        for i, t in enumerate(thumbs):
            sheet.paste(t, ((i % 2) * 640, (i // 2) * 360))
        sheet.save(out / 'sheet.jpg', quality=90)
        print('sheet: %s' % (out / 'sheet.jpg'))
    log = base / 'qconsole.log'
    if log.exists():
        for line in log.read_text(encoding='utf-8', errors='replace').splitlines():
            if any(k in line for k in ('WARNING', 'ERROR', 'could not', "couldn't", 'sun shadow', 'cubemap')):
                print('  log:', line[:160])


if __name__ == '__main__':
    main()
