"""run_client_np.py <cfg> [timeout_s] -- private, muted, localhost-only client run (the stock-skeleton run_client.py
with this work dir's homepath). Reuses the ONE fixed exe of the stock-skeleton bin folder (no new firewall prompt). The window is
moved below the desktop with tools/renderer_bench.py's hide helper (look-only run)."""
from pathlib import Path
REPO = Path(__file__).resolve().parents[4]   # repository root (tools/cloth/pipeline/<stage>/x.py)
import sys, os, time, subprocess, ctypes, ctypes.wintypes as wt
sys.path.insert(0, str(REPO / 'tools'))
from renderer_bench import client_popen, hide_client_windows, foreground_window
W = str(REPO / '.codex-research' / 'eve2b-nativeprop-20261012')
BIN = os.path.join(str(REPO / '.codex-research' / 'eve2b-glm-20261012'), 'bin')
GAME = str(REPO / 'game_directory' / 'Jedi Academy' / 'GameData')
cfg = sys.argv[1]; timeout = int(sys.argv[2]) if len(sys.argv) > 2 else 300
log = os.path.join(W, 'home', 'base', 'qconsole.log')
if os.path.exists(log): os.replace(log, log + '.prev')
cmd = [os.path.join(BIN, 'openjk.x86.exe'), '+set', 'fs_basepath', GAME, '+set', 'fs_homepath', os.path.join(W, 'home'),
       '+set', 's_volume', '0', '+set', 's_musicvolume', '0', '+set', 's_mute', '1', '+set', 'r_fullscreen', '0', '+set', 'r_mode', '-1',
       '+set', 'r_customwidth', '1280', '+set', 'r_customheight', '720', '+set', 'com_hunkMegs', '256',
       '+set', 'sv_pure', '0', '+set', 'net_ip', '127.0.0.1', '+set', 'cl_allowDownload', '0', '+set', 'logfile', '2',
       '+exec', cfg + '.cfg']
fg = foreground_window()
p = client_popen(cmd, BIN)
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
    time.sleep(0.25)
    hide_client_windows(p.pid, fg)
    if not sent and os.path.exists(log):
        try: txt = open(log, encoding='latin1').read()
        except Exception: continue
        if 'EVE_MENU_UP' in txt and '--esc' in sys.argv:
            for h in hwnds(p.pid):
                user32.PostMessageW(h, 0x0100, 0x1B, 0x00010001); time.sleep(0.05)
                user32.PostMessageW(h, 0x0101, 0x1B, 0xC0010001)
            sent = True; print('ESC sent', round(time.time() - t0, 1))
if p.poll() is None:
    print('timeout, killing'); p.kill()
print('exit', p.poll(), 'esc', sent, 'secs', round(time.time() - t0))
