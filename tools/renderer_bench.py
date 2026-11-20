"""Compare JKA renderers on the same demo: frame times, hitches, load time and screenshots.

    python tools/renderer_bench.py list
    python tools/renderer_bench.py build [candidate ...]          # out-of-source builds, engine + renderers
    python tools/renderer_bench.py run --demo kejim-runner [--candidates a,b] [--scenarios default,max] [--reps 3]
    python tools/renderer_bench.py report <run dir>               # (run calls it too)
    python tools/renderer_bench.py presentmon-check               # can PresentMon capture from this shell?

Candidates: tools/fixtures/renderer_bench/candidates.json; settings: scenarios.json. Protocol and caveats:
docs/renderer-bench.md. Each run is a private, muted, offline client: its own bin and homepath under
.codex-research/renderer-bench/, the installed GameData read-only as fs_basepath (game, cgame and ui come from there for
every candidate), net_enabled 0. Timing comes from `timedemo 1`: PresentMon (ETW, per-present frame and GPU times;
needs an elevated shell or the Performance Log Users group) when it can run, and always `com_speeds 1` (one console
line per frame, buffered `logfile 1`); screenshots come from a separate timedemo pass, so they do not disturb the timing. Clients run
one at a time (timing runs alone). Never connects to a server; never writes into GameData or the source clones.
"""
import argparse
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIXTURES = ROOT / 'tools' / 'fixtures' / 'renderer_bench'
ASSETS = ROOT / 'game_directory' / 'Jedi Academy' / 'GameData'
OUT = ROOT / '.codex-research' / 'renderer-bench'
RUNTIME_DLLS = ('SDL2.dll', 'OpenAL32.dll')
# Intel's standalone CLI first (FrameView's bundled copy exits 1 with no message even elevated)
PRESENTMON_PATHS = tuple(p for p in (os.environ.get('PRESENTMON'),) if p) + (   # env PRESENTMON: PresentMon 2.6 CLI
                    r'C:\Program Files\Intel\PresentMon\PresentMon.exe',
                    r'C:\Program Files\NVIDIA Corporation\FrameViewSDK\bin\PresentMon_x64.exe')
FRAME_RE = re.compile(r'frame:\s*\d+\s+all:\s*(\d+)\s+sv:\s*(\d+)\s+ev:\s*(\d+)\s+cl:\s*(\d+)\s+gm:\s*(\d+)\s+rf:\s*(\d+)\s+bk:\s*(\d+)')
TIMEDEMO_RE = re.compile(r'(\d+)\s+frames,?\s+([\d.]+)\s+seconds:?\s+([\d.]+)\s+fps')


# Implicit Vulkan layers on this machine hook every Vulkan client. OBS's graphics hook has no enable switch, so it
# loads into each run; five of eight JKSunny launches exited 0xC0000005 right after "setting mode", with
# no WER entry. Each layer's own disable_environment key turns it off for the client process only.
CLIENT_ENV = {'DISABLE_VULKAN_OBS_CAPTURE': '1', 'EOS_OVERLAY_DISABLE_VULKAN_WIN32': '1',
              'EOS_OVERLAY_DISABLE_VULKAN_WIN64': '1', 'DISABLE_VK_LAYER_VALVE_steam_overlay_1': '1',
              'DISABLE_VK_LAYER_VALVE_steam_fossilize_1': '1'}


# The real cause of those start crashes (an unhandled-exception report in a dev build): EternalJK's
# win32 console attaches to the parent process's console (AttachConsole(ATTACH_PARENT_PROCESS)); under Git Bash
# that is a pseudo console (ConPTY), and the console echo (CON_Show -> WriteConsoleOutputA) faults inside
# KERNELBASE. A client started with its own windowless console cannot attach, and CON_Show returns early.
# The fault is in the engine's CON_Show: its std output handle is not a console screen buffer (Popen redirected it
# to NUL) while GetConsoleWindow() still reports a console, so GetConsoleScreenBufferInfo fails and leaves its struct
# uninitialized. A client gets its own hidden console with its std handles left on it (perf/vulkan also checks the
# call now).
CLIENT_FLAGS = getattr(subprocess, 'CREATE_NEW_CONSOLE', 0)


def client_popen(args, cwd):
    si = None
    if hasattr(subprocess, 'STARTUPINFO'):
        si = subprocess.STARTUPINFO()
        si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        si.wShowWindow = 0   # SW_HIDE: the console window stays hidden
    return subprocess.Popen(args, cwd=cwd, env=client_env(), creationflags=CLIENT_FLAGS, startupinfo=si)


HIDE_POS = (0, 12000)   # below every monitor of the desktop (look-only runs stay out of sight)


def foreground_window():
    if os.name != 'nt':
        return None
    import ctypes
    return ctypes.windll.user32.GetForegroundWindow()


def hide_client_windows(pid, restore_fg=None):
    """Move the client's visible top-level windows below the desktop without activating them, and give the
    foreground back to restore_fg if the client took it. The engine's own vid_xpos/vid_ypos does not survive window
    creation (measured: the window sat at 0,0), so this runs from the launcher, repeatedly (a renderer
    restart creates a new window). Returns how many windows were moved this call."""
    if os.name != 'nt':
        return 0
    import ctypes
    from ctypes import wintypes
    u = ctypes.windll.user32
    # Physical pixels: unaware, Windows scales the coordinates (12000 became 21000 at 175%), the rect never matches
    # and every repeat move resized the window across DPI changes (measured 6720 wide).
    u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))   # PER_MONITOR_AWARE_V2; fails harmlessly once set
    moved = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def each(hwnd, _):
        owner = wintypes.DWORD()
        u.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and u.IsWindowVisible(hwnd):
            r = wintypes.RECT()
            u.GetWindowRect(hwnd, ctypes.byref(r))
            if r.top < HIDE_POS[1] // 2:   # still on the desktop (a new or restarted window)
                # SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
                u.SetWindowPos(hwnd, None, HIDE_POS[0], HIDE_POS[1], 0, 0, 0x0001 | 0x0004 | 0x0010)
                moved.append(hwnd)
        return True

    u.EnumWindows(each, 0)
    if restore_fg:
        fg = u.GetForegroundWindow()
        owner = wintypes.DWORD()
        u.GetWindowThreadProcessId(fg, ctypes.byref(owner))
        if owner.value == pid and fg != restore_fg:
            u.SetForegroundWindow(restore_fg)
    return len(moved)


def client_env():
    return dict(os.environ, **CLIENT_ENV)


def load(name):
    return json.loads((FIXTURES / name).read_text(encoding='utf-8'))


def candidates(names=None):
    cands = load('candidates.json')['candidates']
    for c in cands:      # trees are relative to the repository root (siblings as ../name)
        c['tree'] = str((ROOT / c['tree']).resolve())
    if names:
        wanted = set(names)
        cands = [c for c in cands if c['name'] in wanted]
        missing = wanted - {c['name'] for c in cands}
        if missing:
            raise SystemExit('unknown candidates: %s' % ', '.join(sorted(missing)))
    return cands


def git(tree, *args):
    return subprocess.run(['git', '-C', str(tree), *args], capture_output=True, text=True).stdout.strip()


def build_dir(cand):
    head = git(cand['tree'], 'rev-parse', '--short=9', 'HEAD') or 'nogit'
    dirty = '-dirty' if git(cand['tree'], 'status', '--porcelain', '--untracked-files=no') else ''
    tree_tag = Path(cand['tree']).name
    return OUT / 'builds' / ('%s-%s%s' % (tree_tag, head, dirty)), head + dirty


def cmd_build(args):
    from world_retention import require_free
    require_free(OUT if OUT.exists() else ROOT)
    common = load('candidates.json')['common_cmake_args']
    done = set()
    for cand in candidates(args.candidates):
        if cand.get('build'):
            print('%s is pinned to builds/%s; build its tree through an unpinned candidate' % (cand['name'], cand['build']))
            continue
        bdir, rev = build_dir(cand)
        if cand.get('ref') and not rev.startswith(cand['ref'][:7]):
            print('note: %s is at %s, the setup was written against %s' % (cand['tree'], rev, cand['ref']))
        if bdir in done:
            continue
        done.add(bdir)
        bdir.mkdir(parents=True, exist_ok=True)
        log = bdir.parent / (bdir.name + '.log')
        print('building %s -> %s' % (cand['tree'], bdir), flush=True)
        with open(log, 'w', encoding='utf-8') as f:
            for step in (['cmake', '-S', cand['tree'], '-B', str(bdir), '-G', 'Visual Studio 17 2022', '-A', 'Win32', *common],
                         ['cmake', '--build', str(bdir), '--config', 'RelWithDebInfo', '--', '-m']):
                f.write('$ %s\n' % ' '.join(step))
                f.flush()
                rc = subprocess.run(step, stdout=f, stderr=subprocess.STDOUT).returncode
                if rc:
                    print('  FAILED (%d), see %s' % (rc, log))
                    break
            else:
                (bdir / 'bench_build.json').write_text(json.dumps({'tree': cand['tree'], 'rev': rev,
                    'built': time.strftime('%Y-%m-%dT%H:%M:%S'), 'cmake_args': common}, indent=1), encoding='utf-8')
                print('  ok')


def client_bin(cand, run_dir):
    """Where a run's client starts from: its own bin copy, or for an "installed" candidate the installed GameData
    itself (the one fixed program path allowed through Windows Firewall; nothing is copied there)."""
    return ASSETS if cand.get('installed') else run_dir / 'bin'


def binary_dir(cand, quiet=False):
    """The build for the tree's current commit, else its newest earlier build (a live tree such as xenjk moves on
    between building and running; the run records which build it used). A candidate with "build" names its build
    directory under builds/ instead (one commit of a branch, so several commits can run in one batch). An
    "installed" candidate runs the installed client in place."""
    if cand.get('installed'):
        return ASSETS if (ASSETS / cand['exe']).exists() else None
    if cand.get('build'):
        pinned = OUT / 'builds' / cand['build'] / 'RelWithDebInfo'
        return pinned if (pinned / cand['exe']).exists() else None
    bdir, _ = build_dir(cand)
    out = bdir / 'RelWithDebInfo'
    if (out / cand['exe']).exists():
        return out
    tag = Path(cand['tree']).name
    older = sorted((p for p in (OUT / 'builds').glob(tag + '-*') if p.is_dir() and (p / 'bench_build.json').exists()
                    and (p / 'RelWithDebInfo' / cand['exe']).exists()), key=lambda p: (p / 'bench_build.json').stat().st_mtime)
    if not older:
        return None
    if not quiet:
        print('note: %s uses %s (no build for the tree\'s current commit)' % (cand['name'], older[-1].name))
    return older[-1] / 'RelWithDebInfo'


def bench_settings(cand, scen, settings):
    """The cvars of one run, set on the command line so latched renderer settings apply at start."""
    merged = dict(settings['common'])
    merged.update(settings['scenarios'][scen]['by_family'].get(cand['family'], {}))
    merged['cl_renderer'] = cand['renderer']
    return merged


def bench_cfg(cand, scen, settings, demo_name, shots):
    lines = ['// renderer_bench: %s / %s' % (cand['name'], scen),
             'set com_speeds %d' % (0 if shots else 1), 'set timedemo 1', 'set nextdemo "quit"', 'demo %s' % demo_name]
    last = 0
    for f in shots or ():
        lines += ['wait %d' % (f - last), 'screenshot shot%05d' % f]
        last = f
    return '\n'.join(lines) + '\n'


def stage(cand, run_dir, demo_path):
    bins = binary_dir(cand)
    if not bins:
        raise SystemExit('%s is not built (python tools/renderer_bench.py build %s)' % (cand['name'], cand['name']))
    b = client_bin(cand, run_dir)
    h = run_dir / 'home' / 'base'
    (h / 'demos').mkdir(parents=True, exist_ok=True)
    if not cand.get('installed'):
        b.mkdir(parents=True, exist_ok=True)
        for name in [cand['exe'], *cand['dlls']]:
            shutil.copy2(bins / name, b / name)
        for name in RUNTIME_DLLS:
            if (ASSETS / name).exists():
                shutil.copy2(ASSETS / name, b / name)
    shutil.copy2(demo_path, h / 'demos' / ('bench' + demo_path.suffix))
    return b, h


def find_presentmon(arg):
    if arg == 'off':
        return None
    if arg and arg != 'auto':
        return arg
    return next((p for p in PRESENTMON_PATHS if Path(p).exists()), None)


def start_presentmon(pm, exe, csv_path):
    """A per-run ETW capture of the client's presents. PresentMon needs elevation (or the Performance Log Users
    group); without it, it exits at once and the run falls back to com_speeds."""
    proc = subprocess.Popen([pm, '--process_name', exe, '--output_file', str(csv_path), '--terminate_on_proc_exit',
                             '--no_console_stats', '--session_name', 'xenjk_renderer_bench', '--stop_existing_session'],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    if proc.poll() is not None:
        return None, 'presentmon exited %s at start (not elevated?)' % proc.returncode
    return proc, 'presentmon capturing'


STALL_S = 180


def stop_client(proc):
    """Close the client's windows (WM_CLOSE: the engine quits cleanly and restores what it changed), then kill it
    if it has not gone after 15 s."""
    if proc.poll() is not None:
        return
    try:
        import ctypes
        from ctypes import wintypes
        user32 = ctypes.windll.user32
        hwnds = []

        @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        def each(hwnd, _):
            pid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == proc.pid:
                hwnds.append(hwnd)
            return True
        user32.EnumWindows(each, 0)
        for hwnd in hwnds:
            user32.PostMessageW(hwnd, 0x0010, 0, 0)   # WM_CLOSE
        proc.wait(timeout=15)
    except Exception:
        pass
    if proc.poll() is None:
        proc.kill()
        proc.wait()


def home_file(home, rel):
    """The newest home/*/<rel>: forks write their console log and screenshots into their own game directory, not base."""
    found = sorted(Path(home).glob('*/' + rel), key=lambda q: q.stat().st_mtime)
    return found[-1] if found else Path(home) / 'base' / rel


def launch(cand, run_dir, cfg_text, timeout, sets, presentmon=None):
    b, h = client_bin(cand, run_dir), run_dir / 'home' / 'base'
    (h / 'bench.cfg').write_text(cfg_text, encoding='utf-8')
    # Settings go in autoexec.cfg (run before the renderer starts, so latched ones apply): the engine keeps at most
    # 32 '+' commands from its command line and folds the rest into the last one (a homepath swallowed the line).
    (h / 'autoexec.cfg').write_text(''.join('seta %s "%s"\n' % (k, v) for k, v in sets.items()), encoding='utf-8')
    args = [str(b / cand['exe'])]
    args += [ '+set', 'fs_basepath', str(ASSETS), '+set', 'fs_homepath', str(run_dir / 'home'),
            '+set', 'fs_game', 'base', '+set', 'logfile', '1', '+set', 's_volume', '0', '+set', 's_mute', '1',
            '+set', 'net_enabled', '0', '+set', 'cl_motd', '0', '+exec', 'bench.cfg']
    pm_proc, pm_status = (start_presentmon(presentmon, cand['exe'], run_dir / 'presentmon.csv') if presentmon
                          else (None, 'presentmon off'))
    t0 = time.time()
    proc = client_popen(args, str(b))
    # The engine goes back to the main menu after a demo instead of running `nextdemo`, so the client is stopped
    # here once the timedemo summary is in its log (everything before it is written by then).
    status = 'timeout'
    last_size, last_change = -1, time.time()
    while time.time() - t0 < timeout:
        if proc.poll() is not None:
            status = 'exited %d' % proc.returncode
            break
        try:
            log = home_file(h.parent, 'qconsole.log')
            size = log.stat().st_size
            if size != last_size:
                last_size, last_change = size, time.time()
            if TIMEDEMO_RE.search(log.read_text(encoding='utf-8', errors='replace')):
                time.sleep(1.0)
                status = 'demo done'
                break
        except OSError:
            pass
        if time.time() - last_change > STALL_S:   # an error dialog or a hang: no console output for minutes
            status = 'stalled (no log output for %d s)' % STALL_S
            break
        time.sleep(0.5)
    wall = time.time() - t0
    stop_client(proc)
    if pm_proc:
        try:
            pm_proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            pm_proc.kill()
    return wall, '%s; %s' % (status, pm_status)


def cmd_run(args):
    from world_retention import require_free
    require_free(ROOT, Path(os.environ.get('LOCALAPPDATA', 'C:/')))
    settings = load('scenarios.json')
    demo = settings['demos'].get(args.demo, args.demo)
    src = (ROOT / demo) if not Path(demo).is_absolute() else Path(demo)
    if not src.exists():
        raise SystemExit('demo not found: %s' % src)
    OUT.joinpath('demos').mkdir(parents=True, exist_ok=True)
    demo_copy = OUT / 'demos' / ('%s%s' % (args.demo if args.demo in settings['demos'] else src.stem, src.suffix))
    if not demo_copy.exists():
        shutil.copy2(src, demo_copy)   # copied, never moved: the world keeps its demo
    stamp = time.strftime('%Y%m%dT%H%M%S')
    root = OUT / 'runs' / ('%s-%s' % (stamp, demo_copy.stem))   # a name, never the path given
    scen_names = args.scenarios.split(',')
    pm = find_presentmon(args.presentmon)
    print('presentmon: %s' % (pm or 'off'))
    names = args.candidates.split(',') if args.candidates else load('candidates.json').get('default_run')
    for cand in candidates(names):
        for scen in scen_names:
            for rep in range(args.reps):
                rd = root / ('%s__%s__%d' % (cand['name'], scen, rep))
                stage(cand, rd, demo_copy)
                wall, status = launch(cand, rd, bench_cfg(cand, scen, settings, 'bench', None), args.timeout,
                                    bench_settings(cand, scen, settings), pm)
                (rd / 'run.json').write_text(json.dumps({'candidate': cand['name'], 'scenario': scen, 'rep': rep,
                    'wall_s': round(wall, 2), 'status': status, 'build': str(binary_dir(cand, quiet=True).parent)}), encoding='utf-8')
                print('%s %s rep %d: %s, %.1f s' % (cand['name'], scen, rep, status, wall), flush=True)
            if not args.no_shots:
                rd = root / ('%s__%s__shots' % (cand['name'], scen))
                stage(cand, rd, demo_copy)
                wall, status = launch(cand, rd, bench_cfg(cand, scen, settings, 'bench', settings['shots']['frames']),
                                    args.timeout, bench_settings(cand, scen, settings))
                print('%s %s shots: %s' % (cand['name'], scen, status), flush=True)
    report(root)


def percentile(values, p):
    if not values:
        return float('nan')
    s = sorted(values)
    k = (len(s) - 1) * p / 100.0
    lo, hi = math.floor(k), math.ceil(k)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def parse_log(text):
    """Frame rows from com_speeds and the timedemo summary, from one client's console log."""
    td = list(TIMEDEMO_RE.finditer(text))
    summary = None
    if td:
        n, secs, fps = td[-1].groups()
        summary = {'frames': int(n), 'seconds': float(secs), 'fps': float(fps)}
        text = text[:td[-1].start()]   # playback ends at the summary; menu frames after it are not the demo
    frames = [tuple(int(x) for x in m.groups()) for m in FRAME_RE.finditer(text)]
    if summary and len(frames) > summary['frames']:
        frames = frames[-summary['frames']:]   # and loading frames before it are not either
    return frames, summary


def frame_stats(frames, summary, wall_s=None):
    alls = [f[0] for f in frames]
    out = {'frames_logged': len(alls)}
    if summary:
        out.update(timedemo_fps=summary['fps'], timedemo_s=summary['seconds'])
        if wall_s is not None:
            out['startup_and_load_s'] = round(wall_s - summary['seconds'], 2)
    if alls:
        out.update(mean_ms=round(statistics.mean(alls), 3), p50_ms=percentile(alls, 50), p95_ms=percentile(alls, 95),
                   p99_ms=percentile(alls, 99), max_ms=max(alls),
                   over_16=sum(a > 16 for a in alls), over_33=sum(a > 33 for a in alls), over_50=sum(a > 50 for a in alls),
                   rf_mean_ms=round(statistics.mean(f[5] for f in frames), 3), bk_mean_ms=round(statistics.mean(f[6] for f in frames), 3))
    return out


def parse_presentmon(path, playback_frames=None):
    """Frame times (ms) and GPU times from a PresentMon CSV (2.x FrameTime/GPUTime; 1.x MsBetweenPresents).
    The capture also holds startup and loading; the timedemo's frame count picks the playback: its last frames."""
    import csv
    try:
        with open(path, newline='', encoding='utf-8', errors='replace') as f:
            rows = list(csv.DictReader(f))
    except OSError:
        return None
    if not rows:
        return None
    key = 'FrameTime' if 'FrameTime' in rows[0] else 'MsBetweenPresents'
    gkey = 'GPUTime' if 'GPUTime' in rows[0] else ('MsGPUActive' if 'MsGPUActive' in rows[0] else None)

    def num(r, k):
        try:
            return float(r[k])
        except (KeyError, TypeError, ValueError):
            return None
    if playback_frames and playback_frames < len(rows):
        rows = rows[-playback_frames:]
    ft = [v for v in (num(r, key) for r in rows) if v is not None]
    gt = [v for v in (num(r, gkey) for r in rows) if v is not None] if gkey else []
    if not ft:
        return None
    out = {'pm_frames': len(ft), 'pm_mean_ms': round(statistics.mean(ft), 3), 'pm_p50_ms': round(percentile(ft, 50), 3),
           'pm_p99_ms': round(percentile(ft, 99), 3), 'pm_p999_ms': round(percentile(ft, 99.9), 3),
           'pm_max_ms': round(max(ft), 3), 'pm_over_16': sum(v > 16.7 for v in ft), 'pm_over_33': sum(v > 33.3 for v in ft),
           'pm_over_50': sum(v > 50 for v in ft), 'pm_runtime': rows[0].get('PresentRuntime', ''),
           'pm_mode': rows[0].get('PresentMode', '')}
    if gt:
        out['pm_gpu_mean_ms'] = round(statistics.mean(gt), 3)
    return out


def report(root):
    root = Path(root)
    rows = []
    for rd in sorted(p for p in root.iterdir() if p.is_dir() and not p.name.endswith('__shots')):
        info = json.loads((rd / 'run.json').read_text(encoding='utf-8')) if (rd / 'run.json').exists() else {}
        log = home_file(rd / 'home', 'qconsole.log')
        text = log.read_text(encoding='utf-8', errors='replace') if log.exists() else ''
        frames, summary = parse_log(text)
        row = dict(info, **frame_stats(frames, summary, info.get('wall_s')))
        pmstats = parse_presentmon(rd / 'presentmon.csv', summary['frames'] if summary else None)
        if pmstats:
            row.update(pmstats)
        rows.append(row)
    (root / 'report.json').write_text(json.dumps(rows, indent=1), encoding='utf-8')
    cols = ['candidate', 'scenario', 'rep', 'timedemo_fps', 'pm_mean_ms', 'pm_p99_ms', 'pm_p999_ms', 'pm_max_ms',
            'pm_over_16', 'pm_over_33', 'pm_gpu_mean_ms', 'max_ms', 'over_33', 'rf_mean_ms', 'bk_mean_ms',
            'startup_and_load_s', 'status']
    md = ['| ' + ' | '.join(cols) + ' |', '|' + '---|' * len(cols)]
    for r in rows:
        md.append('| ' + ' | '.join(str(r.get(c, '')) for c in cols) + ' |')
    sheet = contact_sheet(root)
    if sheet:
        md += ['', 'Screenshots: %s' % sheet.name]
    (root / 'report.md').write_text('\n'.join(md) + '\n', encoding='utf-8')
    print('\n'.join(md))


def contact_sheet(root):
    """One image: a row per candidate/scenario, a column per shot frame."""
    try:
        from PIL import Image
    except ImportError:
        return None
    rows = []
    for sd in sorted(p for p in root.iterdir() if p.is_dir() and p.name.endswith('__shots')):
        shots = sorted(home_file(sd / 'home', 'screenshots/shot*.jpg').parent.glob('shot*.jpg'))
        if shots:
            rows.append((sd.name[:-len('__shots')], shots))
    if not rows:
        return None
    w, h = 480, 270
    cols = max(len(s) for _, s in rows)
    sheet = Image.new('RGB', (w * cols, h * len(rows)))
    for y, (_, shots) in enumerate(rows):
        for x, path in enumerate(shots):
            sheet.paste(Image.open(path).convert('RGB').resize((w, h)), (x * w, y * h))
    out = root / 'shots_sheet.jpg'
    sheet.save(out, quality=85)
    (root / 'shots_sheet.txt').write_text('\n'.join(name for name, _ in rows) + '\n', encoding='utf-8')
    return out


def cmd_presentmon_check(args):
    """Capture every process's presents for 5 s and say what PresentMon did (run it in the shell the bench uses)."""
    pm = find_presentmon(args.presentmon)
    if not pm:
        raise SystemExit('no PresentMon found')
    out = OUT / 'presentmon-check.csv'
    OUT.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()
    r = subprocess.run([pm, '--timed', '5', '--terminate_after_timed', '--no_console_stats', '--output_file', str(out),
                        '--session_name', 'xenjk_renderer_bench_check', '--stop_existing_session'],
                       capture_output=True, text=True, errors='replace', timeout=60)
    print('presentmon: %s\nexit code: %s' % (pm, r.returncode))
    for label, text in (('stdout', r.stdout), ('stderr', r.stderr)):
        if text.strip():
            print('%s:\n%s' % (label, text.strip()[:1500]))
    if out.exists():
        rows = out.read_text(encoding='utf-8', errors='replace').splitlines()
        apps = sorted({line.split(',')[0] for line in rows[1:] if line})
        print('csv: %d frames from %s' % (max(0, len(rows) - 1), ', '.join(apps[:8]) or 'nothing'))
        print('columns: %s' % (rows[0][:300] if rows else ''))
    else:
        print('csv: not written')


def cmd_list(args):
    for c in candidates():
        bins = binary_dir(c, quiet=True)
        print('%-14s %-11s %-34s %s' % (c['name'], c['family'], Path(c['tree']).name, 'built' if bins else 'not built'))


def main(argv=None):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('list')
    b = sub.add_parser('build')
    b.add_argument('candidates', nargs='*')
    r = sub.add_parser('run')
    r.add_argument('--demo', default='kejim-runner')
    r.add_argument('--candidates', default='')
    r.add_argument('--scenarios', default='default,max')
    r.add_argument('--reps', type=int, default=3)
    r.add_argument('--timeout', type=int, default=900)
    r.add_argument('--no-shots', action='store_true')
    r.add_argument('--presentmon', default='auto', help="'auto' (FrameView's or Intel's), a path, or 'off'")
    p = sub.add_parser('report')
    p.add_argument('run_dir')
    c = sub.add_parser('presentmon-check')
    c.add_argument('--presentmon', default='auto')
    args = ap.parse_args(argv)
    {'list': cmd_list, 'build': cmd_build, 'run': cmd_run, 'report': lambda a: report(a.run_dir),
     'presentmon-check': cmd_presentmon_check}[args.cmd](args)


if __name__ == '__main__':
    main()
