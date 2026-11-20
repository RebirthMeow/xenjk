"""run_client_npc.py <cfg> [timeout_s] -- private, muted, localhost-only client run for the eve2b_np cloth tests.
Reuses the ONE exe path of the cloth in-game test bin folder (no new firewall prompt; rd-vanilla_x86.dll there is the rebuilt
renderer) with this folder's homepath. The window is moved below the desktop (tools/renderer_bench.py hide helper);
the menu is closed by togglemenu in the cfg. The log is copied to logs/<cfg>.log."""
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import sys, os, time, shutil
sys.path.insert(0, str(REPO / 'tools'))
from renderer_bench import client_popen, hide_client_windows, foreground_window
W = str(REPO / '.codex-research' / 'eve2b-np-cloth-20261012')
BIN = os.path.join(str(REPO / '.codex-research' / 'cloth-ingame-20261012'), 'bin')
GAME = str(REPO / 'game_directory' / 'Jedi Academy' / 'GameData')
cfg = sys.argv[1]; timeout = int(sys.argv[2]) if len(sys.argv) > 2 else 300
log = os.path.join(W, 'home', 'base', 'qconsole.log')
if os.path.exists(log): os.replace(log, log + '.prev')
cmd = [os.path.join(BIN, 'openjk.x86.exe'), '+set', 'fs_basepath', GAME, '+set', 'fs_homepath', os.path.join(W, 'home'),
       '+set', 's_volume', '0', '+set', 's_musicvolume', '0', '+set', 's_mute', '1', '+set', 'r_fullscreen', '0', '+set', 'r_mode', '-1',
       '+set', 'r_customwidth', '1280', '+set', 'r_customheight', '720', '+set', 'com_hunkMegs', '256',
       '+set', 'sv_pure', '0', '+set', 'net_ip', '127.0.0.1', '+set', 'cl_allowDownload', '0', '+set', 'logfile', '2',
       '+set', 'r_swapInterval', '0', '+exec', cfg + '.cfg']
fg = foreground_window()
p = client_popen(cmd, BIN)
t0 = time.time()
while p.poll() is None and time.time() - t0 < timeout:
    time.sleep(0.25)
    hide_client_windows(p.pid, fg)
if p.poll() is None:
    print('timeout, killing'); p.kill(); time.sleep(1)
os.makedirs(os.path.join(W, 'logs'), exist_ok=True)
if os.path.exists(log): shutil.copy(log, os.path.join(W, 'logs', cfg + '.log'))
print('exit', p.poll(), 'secs', round(time.time() - t0))
