"""run_client.py <cfg> [timeout_s] -- private, muted, localhost-only client run.
cfg lives in <work>/home/base/<cfg>.cfg. After the cfg echoes EVE_MENU_UP, one ESC is posted to the game's own hwnd
(closes the Player Configuration menu). The cfg ends with quit. Screenshots are the engine's own `screenshot` command
(written to home/base/screenshots), and PrintWindow(hwnd, hdc, 3) captures are optional via --printwindow."""
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import sys, os, time, subprocess, ctypes, ctypes.wintypes as wt
W = str(REPO / '.codex-research' / 'eve2b-glm-20261012')
GAME = str(REPO / 'game_directory' / 'Jedi Academy' / 'GameData')
cfg = sys.argv[1]; timeout = int(sys.argv[2]) if len(sys.argv) > 2 else 300
log = os.path.join(W, 'home', 'base', 'qconsole.log')
if os.path.exists(log): os.replace(log, log + '.prev')
cmd = [os.path.join(W, 'bin', 'openjk.x86.exe'), '+set', 'fs_basepath', GAME, '+set', 'fs_homepath', os.path.join(W, 'home'),
       '+set', 's_volume', '0', '+set', 's_musicvolume', '0', '+set', 's_mute', '1', '+set', 'r_fullscreen', '0', '+set', 'r_mode', '-1',
       '+set', 'r_customwidth', '1280', '+set', 'r_customheight', '720', '+set', 'com_hunkMegs', '256',
       '+set', 'sv_pure', '0', '+set', 'net_ip', '127.0.0.1', '+set', 'cl_allowDownload', '0', '+set', 'logfile', '2', '+exec', cfg + '.cfg']
p = subprocess.Popen(cmd, cwd=os.path.join(W, 'bin'))
user32 = ctypes.windll.user32
EnumProc = ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
def hwnds(pid):
    res = []
    def cb(h, l):
        q = wt.DWORD(); user32.GetWindowThreadProcessId(h, ctypes.byref(q))
        if q.value == pid and user32.IsWindowVisible(h): res.append(h)
        return True
    user32.EnumWindows(EnumProc(cb), 0); return res
sent = False; t0 = time.time()
while p.poll() is None and time.time() - t0 < timeout:
    time.sleep(0.5)
    if not sent and os.path.exists(log):
        try: txt = open(log, encoding='latin1').read()
        except Exception: continue
        if 'EVE_MENU_UP' in txt:
            for h in hwnds(p.pid):
                user32.PostMessageW(h, 0x0100, 0x1B, 0x00010001); time.sleep(0.05)
                user32.PostMessageW(h, 0x0101, 0x1B, 0xC0010001)
            sent = True; print('ESC sent', time.time() - t0)
if p.poll() is None:
    print('timeout, killing'); p.kill()
print('exit', p.poll(), 'esc', sent, 'secs', round(time.time() - t0))
