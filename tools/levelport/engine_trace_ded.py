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
"""engine_trace_ded.py -- a corpus traced in the engine by the 64-bit dedicated server:
levelcheck_trace is a server command (g_svcmds.c), so the check needs the game module and the collision
model, not a client. No renderer, window, GPU or VRAM, no mouse to keep off, no game-lock slot; at Idle it competes
for CPU only.

    python -I engine_trace_ded.py <map pk3> <map name> <corpus.txt> <out dir> --bin <folder with openjkded.x86_64.exe>
        [--game <jampgamex86_64.dll>] [--pk3 <pk3>]... [--pre "<command after the map loads>"]...
        [--set CVAR=VALUE]... [--idle]

Writes <out>/levelcheck_out.txt (the command's lines, as the client's engine_trace.py writes them) and
<out>/engine_trace.json (traces, trace milliseconds, seconds to loaded, peak private memory). The run home sits on the
packs' drive and links them (never a copy); <out>/run_home.txt names it when it is not <out>/run. The
map loads with cm_chunkManifest 0 (hand-placed chunks: residency off), then the --pre commands (cm_loadchunk lines)
run in order, then the trace; the server polls nothing on a timer: each step is a console marker."""
import argparse
import hashlib
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

import psutil

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import paths  # noqa: E402

# run-home roots, one a drive (by where each really is: the level port may be a junction to another volume);
# retain.py moves the homes registered under the level port's registry with the packs they link
RUN_HOME_ROOTS = (paths.LEVELPORT / 'runhomes', paths.RESEARCH / 'runhomes')
REGISTRY = paths.LEVELPORT / 'runhomes' / 'registry.tsv'      # retain.REGISTRY


def game_data():
    """$XENJK_GAMEDATA, else the nearest game_directory/Jedi Academy/GameData at or above the checkout (a worktree has
    none of its own: the main checkout's)"""
    if os.environ.get('XENJK_GAMEDATA'):
        return paths.GAMEDATA
    for root in [REPO] + list(REPO.parents):
        g = root / 'game_directory' / 'Jedi Academy' / 'GameData'
        if g.is_dir():
            return g
    sys.exit('engine_trace_ded: no game_directory/Jedi Academy/GameData above %s' % REPO)


def drive(p):
    return os.path.splitdrive(os.path.realpath(p))[0].upper()


def run_home(out, packs):
    """<out>/run on the packs' drive, else a home of its own under the RUN_HOME_ROOTS one on that drive"""
    drives = {drive(p) for p in packs}
    if len(drives) != 1:
        sys.exit('engine_trace_ded: the packs are on %s: a run home links them all, so they must share a drive'
                 % ', '.join(sorted(drives)))
    d = drives.pop()
    out.mkdir(parents=True, exist_ok=True)
    if drive(out) == d:
        return out / 'run'
    roots = [r for r in RUN_HOME_ROOTS if drive(r) == d]
    if not roots:
        sys.exit('engine_trace_ded: no run-home root on %s; give an out dir on %s' % (d, d))
    real = os.path.realpath(out)
    home = roots[0] / ('ded-%s-%s' % (Path(real).name[:40], hashlib.sha1(real.lower().encode()).hexdigest()[:10]))
    (out / 'run_home.txt').write_text('%s\n' % home, encoding='utf-8')
    return home


def register(home):
    """a line for retain.py's run-home registry (time, home): it moves a kit's run homes with the kit"""
    try:
        f = REGISTRY
        f.parent.mkdir(parents=True, exist_ok=True)
        with open(f, 'a', encoding='utf-8') as fh:
            fh.write('%s\t%s\n' % (time.strftime('%Y-%m-%d %H:%M:%S'), os.path.realpath(home)))
    except OSError:
        pass


def stage_packs(base, packs):
    base.mkdir(parents=True, exist_ok=True)
    for p in packs:
        dst = base / Path(p).name
        if dst.exists():
            if os.path.samefile(p, dst):
                continue
            sys.exit('engine_trace_ded: %s holds another file of that name; use another out dir' % dst)
        try:
            os.link(p, dst)
        except OSError as e:
            sys.exit('engine_trace_ded: cannot hard-link %s into %s (%s); packs are never copied' % (p, base, e))


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('pk3')
    ap.add_argument('mapname')
    ap.add_argument('corpus')
    ap.add_argument('out')
    ap.add_argument('--bin', required=True, help='a 64-bit build folder with openjkded.x86_64.exe')
    ap.add_argument('--game', help='the game module (default <bin>/jampgamex86_64.dll)')
    ap.add_argument('--pk3', dest='extra', action='append', default=[], help='another pk3 for the run (chunk packs)')
    ap.add_argument('--pre', action='append', default=[], help='a console command after the map loads (cm_loadchunk)')
    ap.add_argument('--set', action='append', default=[], metavar='CVAR=VALUE', help='a cvar set before the map')
    ap.add_argument('--idle', action='store_true', help='Idle priority')
    ap.add_argument('--timeout', type=float, default=3600)
    a = ap.parse_args()
    for kv in a.set:
        if '=' not in kv:
            sys.exit('engine_trace_ded: --set %s: give CVAR=VALUE' % kv)
    out, packs = Path(a.out), [a.pk3] + a.extra
    run = run_home(out, packs)
    base = run / 'home' / 'base'
    stage_packs(base, packs)
    register(run)
    shutil.copy2(a.game or Path(a.bin) / 'jampgamex86_64.dll', base / 'jampgamex86_64.dll')
    shutil.copy2(a.corpus, base / 'levelcheck_in.txt')
    pre = [c for c in a.pre if not re.match(r'(wait\b|devmap\b|map\b|set cm_chunkManifest\b)', c.strip())]
    (base / 'ded_trace.cfg').write_text('\n'.join(['echo LEVELCHECK loaded'] + pre + [
        'echo LEVELCHECK chunks', 'levelcheck_trace levelcheck_in.txt levelcheck_out.txt', 'echo LEVELCHECK done',
        'quit']) + '\n', encoding='utf-8')
    for f in ('levelcheck_out.txt', 'qconsole.log'):
        if (base / f).exists():
            os.replace(base / f, base / (f + '.old'))
    args = [str(Path(a.bin) / 'openjkded.x86_64.exe'), '+set', 'dedicated', '1', '+set', 'fs_basepath',
            str(game_data()), '+set', 'fs_homepath', str((run / 'home').resolve()), '+set', 'fs_game', 'base',
            '+set', 'logfile', '2', '+set', 'sv_pure', '0', '+set', 'bot_enable', '0', '+set', 'net_ip', '127.0.0.1',
            '+set', 'net_port', str(free_port()), '+set', 'g_gametype', '0', '+set', 'sv_cheats', '1',
            '+set', 'cm_chunkManifest', '0']
    for kv in a.set:
        args += ['+set'] + kv.split('=', 1)
    args += ['+map', a.mapname, '+exec', 'ded_trace']
    log = base / 'qconsole.log'
    t0 = time.time()
    flags = (0x00000040 if a.idle else 0) | getattr(subprocess, 'CREATE_NO_WINDOW', 0)   # IDLE_PRIORITY_CLASS
    with open(run / 'home' / 'stdout.txt', 'wb') as so:
        proc = subprocess.Popen(args, cwd=str(a.bin), stdin=subprocess.DEVNULL, stdout=so, stderr=subprocess.STDOUT,
                                creationflags=flags)
        ps = psutil.Process(proc.pid)
        peak, t_loaded, t_chunks = 0, None, None
        try:
            while time.time() - t0 < a.timeout and proc.poll() is None:
                try:
                    peak = max(peak, ps.memory_info().private)
                except psutil.Error:
                    pass
                txt = log.read_text(encoding='utf-8', errors='replace') if log.exists() else ''
                if t_loaded is None and 'LEVELCHECK loaded' in txt:
                    t_loaded = time.time() - t0
                if t_chunks is None and 'LEVELCHECK chunks' in txt:
                    t_chunks = time.time() - t0
                if 'LEVELCHECK done' in txt:
                    break
                time.sleep(0.25)
            proc.wait(timeout=60)
        except subprocess.TimeoutExpired:
            pass
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
    txt = log.read_text(encoding='utf-8', errors='replace') if log.exists() else ''
    m = re.search(r'levelcheck_trace: (\d+) traces -> \S+ in (\d+) ms', txt)
    rep = {'pk3': str(a.pk3), 'map': a.mapname, 'corpus': str(a.corpus), 'host': 'dedicated server',
           'traced': int(m[1]) if m else None, 'trace_ms': int(m[2]) if m else None,
           'seconds_to_loaded': t_loaded, 'seconds_to_chunks_loaded': t_chunks, 'seconds': round(time.time() - t0, 1),
           'peak_private_gb': round(peak / 2 ** 30, 3), 'chunks_loaded': len(re.findall(r'CM_LoadChunk: ', txt)),
           'errors': re.findall(r'.*(?:ERROR|Couldn\'t).*', txt)[:5]}
    if (base / 'levelcheck_out.txt').exists():
        shutil.copy2(base / 'levelcheck_out.txt', out / 'levelcheck_out.txt')
    if run != out / 'run' and log.exists():
        (out / 'run' / 'home' / 'base').mkdir(parents=True, exist_ok=True)
        shutil.copy2(log, out / 'run' / 'home' / 'base' / 'qconsole.log')
    (out / 'engine_trace.json').write_text(json.dumps(rep, indent=1), encoding='utf-8')
    print(json.dumps(rep))
    if not m:
        sys.exit('engine_trace_ded: no trace result in the console log (%s)' % log)


if __name__ == '__main__':
    main()
